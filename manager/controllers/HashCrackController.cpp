//
// Created by Contarr on 10.03.2025.
//

#include "HashCrackController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"
#include "../plugins/MongoPlugin.h"
#include "Alphabet.h"

#include <fstream>
#include <regex>
#include <cmath>
#include <boost/asio/io_context.hpp>
#include <boost/asio/deadline_timer.hpp>

#include <bsoncxx/builder/list.hpp>
#include <bsoncxx/json.hpp>
#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>
#include <amqpcpp/linux_tcp/tcpchannel.h>

#include <amqpcpp/envelope.h>


#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>


using namespace drogon;
using namespace bsoncxx;
using namespace CrackingAlphabet;

using builder::basic::make_document;
using builder::basic::kvp;
using builder::basic::make_array;

void HashCrack::crackInitialize(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {
    LOG_INFO << "Hash cracking initialization for new user on "
             << req->getBody() << "." ;
    auto req_body_json_ptr = req->jsonObject();

    if (!requestValidated(req_body_json_ptr))
    {
        LOG_ERROR << "Validation failed for request "
                  << req->jsonObject()->asString() << "." ;
        callback(makeFailedResponse());
        return;
    }

    std::string uuid = getRandomString(64);
    bool status_save_task = saveTaskInDb(uuid, req_body_json_ptr);
    if (!status_save_task) {
        LOG_ERROR << "Can not add uuid.";
        callback(makeFailedResponse());
        return;
    }
    LOG_INFO << "Task " << uuid << " saved to MongoDB.";
    LOG_INFO << "Sending uuid" << uuid << " to user.";
    callback(HttpResponse::newHttpJsonResponse(Json::Value(uuid)));
    app().getIOLoop(app().getCurrentThreadIndex())
        ->runAfter(0.0, [uuid, req_body_json_ptr, this]() {
            sendTaskToWorkers(uuid, req_body_json_ptr);
        });
    app().getIOLoop(app().getCurrentThreadIndex())->runAfter(
        app().getCustomConfig()["timeout"].asDouble(),
        [uuid, this]() {this->makeJobFail(uuid);});
    LOG_INFO << "Finished sending task " << uuid << " to RabbitMQ.";
}

void HashCrack::getCrackResult(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback,
      const std::string &request_id) {
    LOG_INFO << "Getting crack result for " << request_id << ".";
    try {
        auto client =
            app().getPlugin<MongoPlugin>()->getMongoConnection();
        auto collection = client["MD5HashCrack"]["Results"];
        auto docs = getJobFromDb(collection, request_id);
        if (docs.begin()->empty()) {
            LOG_WARN << "Document not found.";
            callback(makeFailedResponse());
            return;
        }
        Json::Value result;
        for (auto doc : docs) {
            std::string status(doc["Result"].get_string().value);
            result["Status"] = Json::Value(status);
            result["Data"] = Json::Value(Json::arrayValue);
            for (auto password : doc["passwords"].get_array().value) {
                std::string pass(password.get_string().value);
                result["Data"].append(pass);
            }
            result["JobPartitionStatuses"] = Json::Value(Json::arrayValue);
            for (auto parts : doc["workers_done_statuses"].get_array().value) {
                std::string part(parts.get_string().value);
                result["JobPartitionStatuses"].append(part);
            }
            break;
        }
        callback(HttpResponse::newHttpJsonResponse(result));
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during getting crack results for user request "
                  << request_id << ". " << e.what() << ".";
        callback(makeFailedResponse());
    }
}

mongocxx::cursor HashCrack::getJobFromDb(mongocxx::collection &collection,
                                         const std::string &uuid) {
    auto docs =
        collection.find(make_document(kvp(
            "uuid",
            uuid)));
    return docs;
}

bool HashCrack::isNotMD5(const std::string& hash) {
    const std::regex pattern("^[a-fA-F\\d]{32}$");
    return !(std::regex_match(hash, pattern));
}

bool HashCrack::requestValidated(
      const std::shared_ptr<Json::Value> &req_body_json_ptr) {
    if (
        req_body_json_ptr == nullptr ||
        (*req_body_json_ptr)["hash"].isNull() ||
        (*req_body_json_ptr)["maxLength"].isNull() ||
        isNotMD5((*req_body_json_ptr)["hash"].asString()) ||
        !(*req_body_json_ptr)["maxLength"].isInt() ||
        (*req_body_json_ptr)["maxLength"].asInt() < 1)
    {
        return false;
    }
    return true;
}

bool HashCrack::saveTaskInDb(const std::string& uuid,
                             shared_ptr<Json::Value> json_ptr) {
    try {
        auto client = app().getPlugin<MongoPlugin>()->getMongoConnection();
        auto collection = client["MD5HashCrack"]["Results"];

        mongocxx::write_concern wc;
        wc.acknowledge_level(mongocxx::write_concern::level::k_majority);

        mongocxx::options::insert insert_opts;
        insert_opts.write_concern(wc);

        auto doc = buildDocForDbInsertion(uuid, *json_ptr);

        bool insert_status = insertInDb(collection, insert_opts, doc);
        if (!insert_status) {
            LOG_ERROR << "MongoDB insert for task " << uuid
                      << " did not return a result.";
            return false;
        }
    } catch (const std::exception &e) {
        LOG_ERROR << "Failed to save task " << uuid << " in db.";
        return false;
    }
    return true;
}

// TODO: Вынести логику создания данного объекта под ответственность DTO
bsoncxx::document::value HashCrack::buildDocForDbInsertion(
      const std::string &uuid,
      Json::Value json) {
    int workers_count = app().getCustomConfig()["number_of_workers"].asInt();
    auto workers_done_statuses = builder::basic::array{};
    for (int i = 0; i < workers_count; i++) {
        workers_done_statuses.append(WorkerStatusType[kDidNotDistribute]);
    }
    auto creation_time = std::chrono::system_clock::now();
    document::value doc = make_document(
        kvp("uuid", uuid),
        kvp("hash", json["hash"].asString()),
        kvp("maxLength", json["maxLength"].asInt()),
        kvp("Result", JobStatusType[kInProgress]),
        kvp("passwords", make_array()),
        kvp("number_of_workers", workers_count),
        kvp("workers_done_statuses", workers_done_statuses),
        kvp("created_at", types::b_date{creation_time}),
        kvp("updated_at", types::b_date{creation_time}));

    return doc;
}

bool HashCrack::insertInDb(
      mongocxx::collection &collection,
      const mongocxx::options::insert &insert_opts,
      const bsoncxx::document::value &doc) {
    auto result = collection.insert_one(doc.view(), insert_opts);
    if (result.has_value()) {
        return true;
    } else {
        return false;
    }
}

void HashCrack::sendTaskToWorkers(const std::string& uuid,
                                  shared_ptr<Json::Value> json_ptr) {
    try {

        boost::asio::io_context io_context_ptr;
        AMQP::LibBoostAsioHandler handler_ptr(io_context_ptr);
        AMQP::TcpConnection connection_ptr(&handler_ptr, AMQP::Address(
            app().getCustomConfig()["rabbit_host"].asString(),
            app().getCustomConfig()["rabbit_port"].asInt(),
                AMQP::Login(app().getCustomConfig()["rabbit_user"].asString(),
                    app().getCustomConfig()["rabbit_password"].asString()),
            "/"));
        AMQP::TcpChannel channel_ptr(&connection_ptr);

        std::mutex ack_mutex;
        std::condition_variable ack_cv;
        bool ack_received = false;
        bool nack_received = false;

        if (!prepareAmqpChannel(channel_ptr, uuid, ack_mutex, ack_cv,
                           ack_received, nack_received)) {
            LOG_ERROR << "Preparing Amqp Channel failed, stop sending tasks.";
            return;
        }

        if (!declareQueueForChannel(channel_ptr, uuid)) {
            LOG_ERROR << "Declaring Amqp Channel failed, stop sending tasks.";
            return;
        }
        std::thread io_thread([&]() {
            io_context_ptr.run();
            LOG_INFO << "Boost.Asio thread finished sending job pars for "
                     << uuid;
        });
        distributeTask(channel_ptr, uuid, json_ptr, ack_mutex, ack_cv,
                       ack_received, nack_received);

        if (connection_ptr.usable()) {
            connection_ptr.close();
        }
        io_context_ptr.stop(); // Stop the event loop
        if (io_thread.joinable()) {
            io_thread.join();
        }
    }
    catch (std::exception &e) {
        LOG_ERROR << "Error during sending task " << uuid << " to workers: "
        << e.what();
        throw;
    }

}

bool HashCrack::prepareAmqpChannel(
      AMQP::TcpChannel &channel,
      const std::string &uuid,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {
    try {
        channel.confirmSelect()
            .onSuccess([&]() {
                LOG_INFO << "Publisher confirms enabled for task " << uuid;
            })
            .onAck([&](uint64_t deliveryTag, bool) {
                LOG_INFO << "Got delivery from Rabbit on tag " << deliveryTag
                         << " for request " << uuid;
                std::lock_guard lock(ack_mutex);
                ack_received = true;
                ack_cv.notify_one();
            })
            .onNack([&](uint64_t deliveryTag, bool, bool) {
                LOG_WARN << "Didn't get delivery from Rabbit on tag " << deliveryTag
                         << " for request " << uuid;
                std::lock_guard lock(ack_mutex);
                nack_received = true;
                ack_cv.notify_one();
            })
            .onError([&](const char* message) {
                LOG_ERROR << "Error enabling publisher confirms for task "
                          << uuid << ": " << message;
                std::lock_guard lock(ack_mutex);
                nack_received = true; // Treat as NACK
                ack_cv.notify_one();
            });
    } catch (std::exception &e) {
        LOG_ERROR << "Error during preparing amqp channel.";
        return false;
    }
    return true;
}

bool HashCrack::declareQueueForChannel(
      AMQP::TcpChannel &channel,
      const std::string &uuid) {
    try {
        std::string queue_name =
            app().getCustomConfig()["tasks_queue_name"].asString();
        channel.declareQueue(queue_name, AMQP::durable)
            .onSuccess([](const std::string &name,
                             uint32_t messageCount,
                             uint32_t consumerCount) {
                LOG_INFO << "Queue '" << name << "' has been declared with "
                         << messageCount << " messages and "
                         << consumerCount << " consumers";
            })
            .onError([&](const char* message) {
                 LOG_ERROR << "Error declaring queue '" << queue_name
                           << "' for task " << uuid << ": " << message;
            });
    } catch (std::exception &e) {
        LOG_ERROR << "Error during declaring amqp channel.";
        return false;
    }
    return true;
}

void HashCrack::distributeTask(
      AMQP::TcpChannel &channel,
      const std::string &uuid,
      shared_ptr<Json::Value> json_ptr,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {

    int workers_count = app().getCustomConfig()["number_of_workers"].asInt();
    for (int part = 0; part < workers_count; ++part) {
        std::string message =
            buildMessageForRabbit(uuid, *json_ptr, part, workers_count);

        ack_received = false;
        nack_received = false;

        // Retry logic for publishing each part
        bool part_sent =
            trySendingPart(channel, uuid, message, ack_mutex, ack_cv,
                           ack_received, nack_received, part);
        if (part_sent) {
            LOG_INFO << "Successful sent part " << part << " for task " << uuid
                     << " to RabbitMQ";
            makeJobPartWaiting(uuid, part);
            LOG_INFO << "Successful sent part " << part << " for task " << uuid
                     << " to MongoDB";
        } else {
            LOG_WARN << "Failed to send part " << part << " for task " << uuid;
        }
    }
}

std::string HashCrack::buildMessageForRabbit(
      const std::string &uuid,
      const Json::Value &json,
      const int &part,
      const int &total_parts_count) {
    // Construct JSON message for the work unit
    Json::Value work_unit_json =
        ManagerToWorkerDTO(uuid,
                           part,
                           total_parts_count,
                           json["hash"].asString(),
                           json["maxLength"].asInt(),
                           Alphabet).toJson();

    Json::StreamWriterBuilder writer;
    std::string message = writeString(writer, work_unit_json);
    return message;
}

bool HashCrack::trySendingPart(
      AMQP::TcpChannel &channel,
      const std::string &uuid,
      std::string message,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received,
      const int &part) {
    ack_received = false; // Reset before publish
    nack_received = false;
    try {
        bool send_status = publishToRabbit(channel, uuid, message, ack_mutex,
                                           ack_cv, ack_received, nack_received,
                                           part);
        return send_status;
    } catch (const AMQP::Exception& e) {
        LOG_ERROR << "RabbitMQ publish exception for task " << uuid
                  << ", part " << part << ": " << e.what();
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR << "Generic exception during RabbitMQ publish for task "
                  << uuid << ", part " << part << ": " << e.what();
        return false;
    }
}

bool HashCrack::publishToRabbit(
      AMQP::TcpChannel &channel,
      const std::string &uuid,
      std::string message,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received,
      const int &part) {
    std::string queue_name =
            app().getCustomConfig()["tasks_queue_name"].asString();
    AMQP::Envelope envelope(message);
    envelope.setDeliveryMode(2);
    int ttl = app().getCustomConfig()["timeout"].asInt();
    envelope.setExpiration(to_string( ttl * 1000));
    channel.publish("", queue_name, envelope);
    LOG_INFO << "Published part " << part << " for task " << uuid;

    std::unique_lock lock(ack_mutex);
    auto wait_duration =
        std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::seconds(5));

    if (ack_cv.wait_for(lock, wait_duration, [&] {
            return ack_received || nack_received;
        })) {
        if (ack_received) {
            LOG_INFO << "ACK received for part " << part
                     << "on task " << uuid;
            return true; // Part sent successfully
        } else {
            LOG_WARN << "NACK received for part " << part
                     << " on task " << uuid;
            return false;
        }
    } else {
        LOG_WARN << "Timeout waiting for ACK/NACK for part " << part
                 << " on task " << uuid;
        return false;
    }
}

void HashCrack::makeJobPartWaiting(const std::string &uuid, const int &part) {
    std::string part_number = to_string(part);
    try {
        auto client = app().getPlugin<MongoPlugin>()->getMongoConnection();
        auto collection = client["MD5HashCrack"]["Results"];

        auto filter = make_document(kvp("$and", make_array(
            make_document(kvp("uuid", uuid)),
            make_document(kvp("workers_done_statuses." + part_number,
                WorkerStatusType[kDidNotDistribute])))));
        auto update = make_document(kvp("$set", make_document(
            kvp("workers_done_statuses." + part_number,
                WorkerStatusType[kWaiting]),
            kvp("updated_at",
                types::b_date{std::chrono::system_clock::now()}))));
        mongocxx::options::update opts;
        mongocxx::write_concern wc;
        wc.acknowledge_level(mongocxx::write_concern::level::k_majority);
        opts.write_concern(wc);
        auto result =
            collection.update_one(filter.view(), update.view(), opts);

        if (!result.has_value() || result->modified_count() == 0) {
            throw std::runtime_error("Failed to set worker status " \
                "WAITING on " + part_number + " part for " + uuid);
    }

    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to update task status  for " << uuid
                  << ": " << e.what();
    }
}

void HashCrack::makeJobFail(const std::string &uuid) {
    try {
        auto client = app().getPlugin<MongoPlugin>()->getMongoConnection();
        auto collection = client["MD5HashCrack"]["Results"];

        auto filter_for_fail = make_document(kvp("uuid", uuid));

        auto filter_for_error = makeFilterForFinalType(uuid, kFailed, "");
        auto update_for_fail = make_document(kvp("$set", make_document(
            kvp("workers_done_statuses.$[elem]", WorkerStatusType[kFailed]),
            kvp("updated_at",
                types::b_date{std::chrono::system_clock::now()}))));
        auto update_for_error = makeUpdateForFinalType(kFailed, kError,
            "", builder::basic::array());

        mongocxx::options::update opts;
        bsoncxx::array::value array_filter =
            make_array(make_document(kvp("elem", make_document(
                kvp("$in", make_array(
                    WorkerStatusType[kWaiting],
                    WorkerStatusType[kDidNotDistribute]))))));
        opts.array_filters(array_filter.view());
        mongocxx::write_concern wc;
        wc.acknowledge_level(mongocxx::write_concern::level::k_majority);
        opts.write_concern(wc);

        updateJobStatusInDb(uuid, collection, filter_for_fail,
            filter_for_error, update_for_fail, update_for_error, opts, kError);

    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to update task status  for " << uuid
                  << ": " << e.what();
    }
}

document::value HashCrack::makeFilterForFinalType(
      const std::string &uuid,
      WorkersStatus &&worker_status,
      const std::string &part_number) {
    int workers_count = app().getCustomConfig()["number_of_workers"].asInt();
    if (worker_status == kFailed) {
        auto array_all = builder::basic::array{};
        array_all.append(make_document(kvp("uuid", uuid)));
        for (int i = 0; i < workers_count; ++i) {
            array_all.append(make_document(kvp("$or", make_array(
                make_document(kvp("workers_done_statuses." + to_string(i),
                    WorkerStatusType[kWaiting])),
                make_document(kvp("workers_done_statuses." + to_string(i),
                    WorkerStatusType[kDidNotDistribute]))))));
        }
        auto result = make_document(kvp("$and", array_all));
        return result;
    } else {
        auto array_all = builder::basic::array{};
        array_all.append(make_document(kvp("uuid", uuid)));
        array_all.append(make_document(kvp(
            "workers_done_statuses." + part_number,
            WorkerStatusType[kWaiting])));
        for (int i = 0; i < workers_count; ++i) {
            if (i == stoi(part_number)) {
                continue;
            }
            array_all.append(make_document(kvp(
                "workers_done_statuses." + to_string(i),
                WorkerStatusType[kDone])));
        }
        auto result = make_document(kvp("$and", array_all));
        return result;
    }
}

document::value HashCrack::makeUpdateForFinalType(
      WorkersStatus &&worker_status,
      StatusCode &&status_code,
      const std::string &part_number,
      const builder::basic::array &passwords) {
    if (status_code == kError) {
        auto result = make_document(kvp("$set", make_document(
                kvp("workers_done_statuses.$[elem]",
                    WorkerStatusType[worker_status]),
                kvp("Result", JobStatusType[status_code]),
                kvp("updated_at",
                    types::b_date{std::chrono::system_clock::now()}))));

        return result;
    } else {
        auto result = make_document(kvp("$set", make_document(
            kvp("workers_done_statuses." + part_number,
                WorkerStatusType[status_code]),
            kvp("Result", JobStatusType[worker_status]),
            kvp("$addToSet", make_document(
                kvp("passwords",
                    make_document(kvp("$each", passwords))))),
                    kvp("updated_at",
                        types::b_date{std::chrono::system_clock::now()}))));
        return result;
    }
}

void HashCrack::updateJobStatusInDb(const std::string &uuid,
                         mongocxx::collection &collection,
                         const bsoncxx::document::value &filter_one,
                         const bsoncxx::document::value &filter_all,
                         const bsoncxx::document::value &update_one,
                         const bsoncxx::document::value &update_all,
                         const mongocxx::options::update &opts,
                         StatusCode &&status) {
    auto result =
            collection.update_one(filter_all.view(), update_all.view(), opts);
    if (result.has_value() && result->modified_count() > 0) {
        LOG_INFO << "Changed job " << uuid << " status to "
                 << JobStatusType[status];
        return;
    }
    result =
        collection.update_one(filter_one.view(), update_one.view(), opts);
    if (result.has_value() && result->modified_count() > 0) {
        LOG_INFO << "Changed job " << uuid << " status to "
                 << JobStatusType[status];
        return;
    }
    LOG_INFO << "Nothing to update on job " << uuid << " status";
}


HttpResponsePtr HashCrack::makeFailedResponse() {
    Json::Value json;
    json["error"] = true;
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(k500InternalServerError);
    return resp;
}

std::string HashCrack::getRandomString(size_t n) {
    std::vector<unsigned char> random(n);
    utils::secureRandomBytes(random.data(), random.size());

    // This is cryptographically safe as 256 mod 16 == 0
    const std::string char_set = "0123456789abcdefghkjklmnopqrstuv";
    assert(256 % char_set.size() == 0);
    std::string random_string(n, '\0');
    for (size_t i = 0; i < n; i++)
        random_string[i] = char_set[random[i] % char_set.size()];
    return random_string;
}