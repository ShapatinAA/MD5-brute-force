//
// Created by Contarr on 25.04.2025.
//

#include "StartupPlugin.h"
#include "Alphabet.h"
#include "CrackStatuses.h"
#include "ManagerToWorkerDTO.h"

#include <drogon/drogon.h>
#include <mongocxx/uri.hpp>
#include <iostream>
#include <WorkerToManagerDTO.h>
#include <amqpcpp/address.h>
#include <amqpcpp/exception.h>
#include <amqpcpp/flags.h>
#include <amqpcpp/libboostasio.h>
#include <amqpcpp/login.h>
#include <boost/asio/io_context.hpp>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/exception/query_exception.hpp>

#include "MongoPlugin.h"

using namespace bsoncxx;
using namespace CrackStatuses;
using namespace CrackingAlphabet;
using namespace std::chrono;
using namespace drogon;

using builder::basic::make_document;
using builder::basic::kvp;
using builder::basic::make_array;


void StartupPlugin::initAndStart(const Json::Value& config) {
    auto custom_config = app().getCustomConfig();
    kRabbitHost = custom_config["rabbit_host"].asString();
    kRabbitPort = custom_config["rabbit_port"].asInt();
    kRabbitUserName = custom_config["rabbit_user"].asString();
    kRabbitPassword = custom_config["rabbit_password"].asString();
    kUpdateTime = config["update_time"].asDouble();
    kResultQueueName = custom_config["results_queue_name"].asString();
    kTasksQueueName = custom_config["tasks_queue_name"].asString();
    kTimeout = custom_config["timeout"].asDouble();
    kWorkersCount = custom_config["number_of_workers"].asInt();
    LOG_INFO << "Startup plugin initialized successfully.";
}
void StartupPlugin::shutdown() {
    LOG_INFO << "Startup plugin stopped.";
}

void StartupPlugin::resumeWork() {
    try {
        auto client =
            drogon::app().getPlugin<MongoPlugin>()->getMongoConnection();
        auto collection = client["MD5HashCrack"]["Results"];
        retrieveReadyResults(collection);
        auto docs = getWaitingAndUndistributedJobParts(collection);
        for (auto doc : docs) {
            dealWithDoc(collection, doc);
        }
    } catch (mongocxx::query_exception e) {
        LOG_ERROR << "Exception occured while querying: " << e.what();
    }
    drogon::app().getLoop()->runAfter(kUpdateTime, []() {
        drogon::app().getPlugin<StartupPlugin>()->resumeWork();
    });
}

void StartupPlugin::retrieveReadyResults(mongocxx::collection &collection) {
    LOG_INFO << "Running periodic results queue check...";
    try {
        boost::asio::io_context io_context_ptr;
        AMQP::LibBoostAsioHandler handler_ptr(io_context_ptr);
        AMQP::TcpConnection connection_ptr(&handler_ptr,
            AMQP::Address(kRabbitHost, kRabbitPort,
                AMQP::Login(kRabbitUserName, kRabbitPassword),
            "/"));
        AMQP::TcpChannel channel_ptr(&connection_ptr);

        if (!declareQueueForRead(channel_ptr)) {
            LOG_ERROR << "Declaring Amqp Channel failed, stop sending tasks.";
            return;
        }
        std::thread io_thread([&]() {
            io_context_ptr.run();
            LOG_INFO << "Boost.Asio thread finished retrieving jobs results.";
        });

        readAndProcessResultQueue(collection, channel_ptr);

        if (connection_ptr.usable()) {
            connection_ptr.close();
        }
        io_context_ptr.stop(); // Stop the event loop
        if (io_thread.joinable()) {
            io_thread.join();
        }

    } catch (const std::exception &e) {
        LOG_ERROR << "Error during retrieve of ready results." << e.what();
        throw;
    }

}

bool StartupPlugin::declareQueueForRead(
    AMQP::TcpChannel &channel_ptr) {
    try {
        LOG_INFO << "Trying to declare queue.";
        channel_ptr.declareQueue(kResultQueueName, AMQP::durable)
            .onSuccess([&](const std::string& name, uint32_t, uint32_t) {
                LOG_INFO << "Results queue '" << name
                         << "' checked/declared ok.";
            })
            .onError([&](const char* message) {
                LOG_ERROR << "Failed to declare results queue '"
                          << kResultQueueName << "': " << message;
            });
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during declaring amqp channel.";
        return false;
    }
    return true;
}

void StartupPlugin::readAndProcessResultQueue(
      mongocxx::collection &collection,
      AMQP::TcpChannel &channel_ptr) {

    bool queue_reported_empty = false;
    bool got_error = false;
    bool consumed = false;

    while (true) {

        std::mutex queue_empty_mutex;
        std::condition_variable queue_empty_cv;
        queue_reported_empty = false;
        got_error = false;
        consumed = false;
        if (!getAndProcessMessageFromQueue(
            collection, channel_ptr, queue_empty_mutex, queue_empty_cv,
            queue_reported_empty, got_error, consumed)) {
            LOG_ERROR << "Error reading and processing result queue.";
            return;
        }
        std::unique_lock lock(queue_empty_mutex);

        queue_empty_cv.wait(lock, [&] {
            return queue_reported_empty || got_error || consumed;
        });
        if (queue_reported_empty) {
            LOG_INFO << "Finished reading results queue.";
            break;
        }
        if (got_error) {
            LOG_ERROR << "Error during Results queue reading.";
            return;
        }
    }
}

bool StartupPlugin::getAndProcessMessageFromQueue(
      mongocxx::collection &collection,
      AMQP::TcpChannel &channel_ptr,
      std::mutex &queue_empty_mutex,
      std::condition_variable &queue_empty_cv,
      bool &queue_reported_empty,
      bool &got_error,
      bool &consumed) {
    try {
        channel_ptr.get(kResultQueueName)
            .onReceived([&](const AMQP::Message& message,
                               uint64_t deliveryTag, bool) {
                LOG_INFO << "Fetched message tag " << deliveryTag;
                processMessageFromQueue(message, channel_ptr,
                                        deliveryTag, collection);
                LOG_INFO << "Processed message from " << "resultsQueue"
                         << ". deliveryTag: " << deliveryTag;
                std::lock_guard lock(queue_empty_mutex);
                consumed = true;
                queue_empty_cv.notify_one();
            })
            .onEmpty([&]() {
                LOG_INFO << "Queue " << kResultQueueName << " reported empty.";
                std::lock_guard lock(queue_empty_mutex);
                queue_reported_empty = true;
                queue_empty_cv.notify_one();
            })
            .onError([&](const char* message) {
                LOG_ERROR << "Error during channel.get(): " << message;
                std::lock_guard lock(queue_empty_mutex);
                got_error = true;
                queue_empty_cv.notify_one();
            });
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during getting and processing task from rabbitmq.";
        return false;
    }
    return true;
}

void StartupPlugin::processMessageFromQueue(
      const AMQP::Message& message,
      AMQP::TcpChannel &channel_ptr,
      uint64_t deliveryTag,
      mongocxx::collection &collection) {
    std::string body(message.body(), message.bodySize());
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(body, root)) {
        throw std::runtime_error("Failed to parse result JSON: "
            + reader.getFormattedErrorMessages());
    }
    validateRetrieveMessage(root);
    WorkerToManagerDTO result(root);
    try {
        makeJobPartDone(collection, result);
        channel_ptr.ack(deliveryTag);
        LOG_INFO << "ACKed tag " << deliveryTag << " (UUID: "
                 << result.getRequestId() << ")";
    } catch (const std::exception& e) {
        LOG_ERROR << "MongoDB processing failed for tag " << deliveryTag
                  << " uuid: " << result.getRequestId() << ". " << e.what()
                  << "Message Body: " << body;
        try {
            channel_ptr.reject(deliveryTag);
            LOG_WARN << "Rejected invalid message tag " << deliveryTag;
        } catch(const std::exception& ex) {
            LOG_ERROR << "Failed to reject message tag " << deliveryTag
                      << " uuid: " << result.getRequestId() << " " << ex.what();
        }
    }
}

void StartupPlugin::validateRetrieveMessage(const Json::Value &root) {
    // Basic validation
    if (!root.isMember("RequestId") || !root["RequestId"].isString() ||
        !root.isMember("PartNumber") || !root["PartNumber"].isInt() ||
        !root.isMember("Answer") || !root["Answer"].isArray()) {
        throw std::runtime_error("Missing/invalid fields");
    }
}

void StartupPlugin::makeJobPartDone(mongocxx::collection &collection,
                                    const WorkerToManagerDTO &message) {
    std::string uuid = message.getRequestId();
    std::string part_number = to_string(message.getPartNumber());
    auto passwords = builder::basic::array{};
    for (std::string s : message.getAnswer()) {
        passwords.append(s);
    }
    try {
        auto filter_for_done = make_document(kvp("$and", make_array(
            make_document(kvp("uuid", uuid)),
            make_document(kvp("workers_done_statuses." + part_number,
                              WorkerStatusType[kWaiting])))));

        auto filter_for_ready =
            makeFilterForFinalType(uuid, kDone, part_number);

        auto update_for_done = makeUpdateForFinalType(kDone, kPartialResult,
            part_number, passwords);

        auto update_for_ready = makeUpdateForFinalType(kDone, kReady,
            part_number, passwords);

        mongocxx::write_concern wc;
        wc.acknowledge_level(mongocxx::write_concern::level::k_majority);
        mongocxx::options::update opts;
        opts.write_concern(wc);

        updateJobStatusInDb(uuid, collection, filter_for_done,
            filter_for_ready, update_for_done, update_for_ready, opts, kReady);

    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to update task status  for " << uuid
                  << ": " << e.what();
        throw;
    }
}

mongocxx::cursor StartupPlugin::getWaitingAndUndistributedJobParts(
      mongocxx::collection &collection) {
    auto docs =
        collection.find(make_document(kvp(
            "workers_done_statuses",
            make_document(kvp("$elemMatch", make_document(
                kvp("$in", make_array(WorkerStatusType[kWaiting], WorkerStatusType[kDidNotDistribute]))))))
            ));
    return docs;
}

void StartupPlugin::dealWithDoc(mongocxx::collection &collection,
                                const bsoncxx::document::view &doc) {
    std::string uuid (doc["uuid"].get_string().value);
    auto ts = doc["created_at"].get_date().value;
    system_clock::time_point created{ milliseconds{ts} };
    auto diff = system_clock::now() - created;
    if (duration_cast<seconds>(diff).count() >= kTimeout) {
        makeJobFail(collection, uuid);
        return;
    }
    sendTaskToWorkers(uuid, doc, collection);
}

void StartupPlugin::makeJobFail(mongocxx::collection &collection,
                                const std::string &uuid) {
    try {
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
            make_array(make_document(kvp("elem",
                make_document(kvp("$in",
                    make_array(WorkerStatusType[kWaiting],
                        WorkerStatusType[kDidNotDistribute]))))));
        opts.array_filters(array_filter.view());
        mongocxx::write_concern wc;
        wc.acknowledge_level(mongocxx::write_concern::level::k_majority);
        opts.write_concern(wc);

        updateJobStatusInDb(uuid, collection, filter_for_fail,
            filter_for_error, update_for_fail, update_for_error, opts, kError);

    } catch (const std::exception& e) {
        LOG_ERROR << "Failed to update task status  for " << uuid.data()
                  << ": " << e.what();
    }

}

document::value StartupPlugin::makeFilterForFinalType(
      const std::string &uuid,
      WorkersStatus &&worker_status,
      const std::string &part_number
      ) {
    if (worker_status == kFailed) {
        auto array_all = builder::basic::array{};
        array_all.append(make_document(kvp("uuid", uuid)));
        for (int i = 0; i < kWorkersCount; ++i) {
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
        for (int i = 0; i < kWorkersCount; ++i) {
            if (i == stoi(part_number)) {
                continue;
            }
            array_all.append(make_document(kvp("workers_done_statuses."
                + to_string(i), WorkerStatusType[kDone])));
        }
        auto result = make_document(kvp("$and", array_all));
        return result;
    }
}

document::value StartupPlugin::makeUpdateForFinalType(
      WorkersStatus &&worker_status,
      StatusCode &&status_code,
      const std::string &part_number,
      const builder::basic::array &passwords) {
    if (status_code == kError) {
        auto result = make_document(kvp("$set", make_document(
                kvp("workers_done_statuses.$[elem]",
                    WorkerStatusType[worker_status]),
                kvp("Result", JobStatusType[status_code]),
                kvp("updated_at", types::b_date{system_clock::now()}))));

        return result;
    } else {
        auto result = make_document(
            kvp("$set", make_document(
                kvp("workers_done_statuses." + part_number,
                    WorkerStatusType[worker_status]),
                kvp("Result", JobStatusType[status_code]),
                kvp("updated_at",
                    types::b_date{system_clock::now()}))),
            kvp("$addToSet", make_document(
                kvp("passwords",
                    make_document(kvp("$each", passwords))))));
        return result;
    }
}

void StartupPlugin::updateJobStatusInDb(
      const std::string &uuid,
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

void StartupPlugin::sendTaskToWorkers(const std::string& uuid,
                                      const bsoncxx::document::view &doc,
                                      mongocxx::collection &collection) {
    try {
        boost::asio::io_context io_context_ptr;
        AMQP::LibBoostAsioHandler handler_ptr(io_context_ptr);
        AMQP::TcpConnection connection_ptr(&handler_ptr,
            AMQP::Address(kRabbitHost, kRabbitPort,
                AMQP::Login(kRabbitUserName, kRabbitPassword),
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
            LOG_INFO << "Boost.Asio thread finished.";
        });

        distributeTask(channel_ptr, collection, uuid, doc, ack_mutex, ack_cv,
                       ack_received, nack_received);


        if (connection_ptr.usable()) {
            connection_ptr.close();
        }
        io_context_ptr.stop(); // Stop the event loop
        if (io_thread.joinable()) {
            io_thread.join();
        }
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during sending task " << uuid << " to workers: "
        << e.what();
        throw;
    }
}

bool StartupPlugin::prepareAmqpChannel(
      AMQP::TcpChannel &channel_ptr,
      const std::string &uuid,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {
    try {
        channel_ptr.confirmSelect()
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
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during preparing amqp channel.";
        return false;
    }
    return true;
}

bool StartupPlugin::declareQueueForChannel(
      AMQP::TcpChannel &channel_ptr,
      const std::string &uuid) {
    try {
        channel_ptr.declareQueue(kTasksQueueName, AMQP::durable)
            .onSuccess([](const std::string &name,
                             uint32_t messageCount,
                             uint32_t consumerCount) {
                LOG_INFO << "Queue '" << name << "' has been declared with "
                         << messageCount << " messages and "
                         << consumerCount << " consumers";
            })
            .onError([&](const char* message) {
                 LOG_ERROR << "Error declaring queue '" << kTasksQueueName
                           << "' for task " << uuid << ": " << message;
            });
    } catch (const std::exception &e) {
        LOG_ERROR << "Error during declaring amqp channel.";
        return false;
    }
    return true;
}

void StartupPlugin::distributeTask(
      AMQP::TcpChannel &channel_ptr,
      mongocxx::collection &collection,
      const std::string &uuid,
      const bsoncxx::document::view &doc,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {

    auto statuses = doc["workers_done_statuses"].get_array().value;
    int length = std::distance(statuses.begin(), statuses.end());
    for (int part = 0; part < length; ++part) {
        if (statuses[part].get_string().value
            == WorkerStatusType[kDidNotDistribute]) {

            std::string message =
                buildMessageForRabbit(uuid, doc, part, statuses.length());

            ack_received = false;
            nack_received = false;

            // Retry logic for publishing each part
            bool part_sent =
                trySendingPart(channel_ptr, uuid, message, ack_mutex, ack_cv,
                               ack_received, nack_received, part);
            if (part_sent) {
                LOG_INFO << "Successful sent part " << part << " for task "
                         << uuid << " to RabbitMQ";
                makeJobPartWaiting(collection, uuid, part);
                LOG_INFO << "Successful sent part " << part << " for task "
                         << uuid << " to MongoDB";
            } else {
                LOG_WARN << "Failed to send part " << part << " for task " << uuid;
            }
        }
    }
}

std::string StartupPlugin::buildMessageForRabbit(
      const std::string &uuid,
      const bsoncxx::document::view &doc,
      const int &part,
      const int &total_parts_count) {
    LOG_INFO << "7";
    // Construct JSON message for the work unit
    Json::Value work_unit_json =
        ManagerToWorkerDTO(uuid,
                           part,
                           total_parts_count,
                           doc["hash"].get_string().value,
                           doc["maxLength"].get_int32().value,
                           Alphabet).toJson();

    Json::StreamWriterBuilder writer;
    std::string message = writeString(writer, work_unit_json);
    return message;
}

bool StartupPlugin::trySendingPart(
      AMQP::TcpChannel &channel_ptr,
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
        bool send_status = publishToRabbit(channel_ptr, uuid, message,
                                           ack_mutex, ack_cv, ack_received,
                                           nack_received, part);
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

bool StartupPlugin::publishToRabbit(
      AMQP::TcpChannel &channel_ptr,
      const std::string &uuid,
      std::string message,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received,
      const int &part) {
    AMQP::Envelope envelope(message);
    envelope.setDeliveryMode(2);
    envelope.setExpiration("x-message-ttl = " +
        to_string(static_cast<int>(kTimeout * 1000)));
    channel_ptr.publish("", kTasksQueueName, envelope);
    LOG_INFO << "Published part " << part << " for task " << uuid;

    std::unique_lock lock(ack_mutex);
    auto wait_duration =
        std::chrono::duration_cast<seconds>(
        seconds(1));

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

void StartupPlugin::makeJobPartWaiting(mongocxx::collection &collection,
                                       const std::string &uuid,
                                       const int &part) {
    std::string part_number = to_string(part);
    try {
        auto filter = make_document(kvp("$and", make_array(
            make_document(kvp("uuid", uuid)),
            make_document(kvp("workers_done_statuses." + part_number,
                WorkerStatusType[kDidNotDistribute])))));
        auto update = make_document(kvp("$set", make_document(
            kvp("workers_done_statuses." + part_number,
                WorkerStatusType[kWaiting]),
            kvp("updated_at",
                types::b_date{system_clock::now()}))));
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
        LOG_FATAL << "Failed to update task status  for " << uuid
                  << ": " << e.what();
        throw;
    }
}







