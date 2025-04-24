//
// Created by Contarr on 10.03.2025.
//

#include "HashCrackController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"
#include "../plugins/MongoPlugin.h"

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


#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>


using namespace drogon;

using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::basic::kvp;

/*
 * TODO:
 *  - Добавить ограничение на кол-во добавляемых в коллекцию элементов,
 *  - тем самым реализовав ограниченную очередь;
 *  - для этого уже есть переменная maxStoreSize_,
 *  - остаётся внедрить проверку по ней в функцию.
*/
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
    // app().getIOLoop(app().getCurrentThreadIndex())->runAfter(
    //     kTimeout,
    //     [uuid, this]() {this->makeTasksFail(uuid);});
    app().getIOLoop(app().getCurrentThreadIndex())->runAfter(
        60.0,
        [uuid, this]() {this->makeTasksFail(uuid);});
    bool status_send_task = sendTaskToWorkers(uuid, req_body_json_ptr);
    if (!status_send_task) {
        LOG_ERROR << "Failed to send task " << uuid << " to RabbitMQ.";
        makeTasksFail(uuid);
        return;
    }
    LOG_INFO << "Task with uuid" << uuid << " successfully sent to RabbitMQ.";
}

void HashCrack::getCrackResult(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback,
      const std::string &request_id) {
    LOG_INFO << "Getting crack result for " << request_id << ".";
    std::unique_lock ulock(crack_result_store_mtx_);
    if (crack_result_store_.find(request_id) == crack_result_store_.end()) {
        LOG_ERROR << "No result was found for " << request_id << ".";
        callback(makeFailedResponse());
        return;
    }

    auto crack_result = crack_result_store_[request_id];
    ulock.unlock();
    setProgressValue(crack_result, request_id);
    LOG_INFO << "Sending crack result for "
             << request_id << " to user.";
    std::lock_guard lock(crack_result->mtx);
    Json::Value json(crack_result->result);
    callback(HttpResponse::newHttpJsonResponse(std::move(json)));
}

//TODO:
// - Валидация запроса. Сравнение ip-адреса, с которого получили реквест,
// - со списком адресов.
void HashCrack::processTaskResponde(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {
    auto response = WorkerToManagerDTO(*req->jsonObject());
    LOG_INFO << "Getting responce from worker " << response.getPartNumber()
             << " on request number " << response.getRequestId() << ".";
    callback(HttpResponse::newHttpResponse());

    std::unique_lock lock(crack_result_store_mtx_);
    auto crack_result=
        crack_result_store_[response.getRequestId()];
    lock.unlock();

    if (checkIfTimeout(crack_result, response)) {
        return;
    }

    LOG_INFO << "Changing status of " << response.getRequestId()
             << " and adding data." ;
    std::lock_guard lock_res(crack_result->mtx);
    crack_result->workers[response.getPartNumber()] = kDone;
    if (crack_result->result["status"] == JobStatusType[kInProgress]) {
        crack_result->result["status"] = JobStatusType[kPartialResult];
    }
    for (const auto& word : response.getAnswer()) {
        crack_result->result["data"].append(word);
    }

    for (auto worker : crack_result->workers) {
        if (worker.second == kWaiting ||
            worker.second == kFailed) {
            return;
        }
    }
    LOG_INFO << "Setting status READY to job with number "
             << response.getRequestId() << ".";
    crack_result->result["status"] = JobStatusType[kReady];
    std::lock_guard lock_store(request_store_mtx_);
    if (request_store_.find(response.getRequestId()) != request_store_.end()) {
        auto request = request_store_.at(response.getRequestId());
        std::lock_guard lock_req(request->mtx);
        request_store_.erase(response.getRequestId());
    }
    LOG_INFO << "Erased " << response.getRequestId() << " from requests store.";
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
    // std::optional<bsoncxx::oid> inserted_id; // Если понадобится id объекта

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
    return true;
}

// TODO: Вынести логику создания данного объекта под ответственность DTO
bsoncxx::document::value HashCrack::buildDocForDbInsertion(
      const std::string &uuid,
      Json::Value json) {
    // int number_of_workers = kConfig["numberOfWorkers"].asInt();
    int number_of_workers = 4;
    auto workers_done_statuses = bsoncxx::builder::basic::array{};
    for (int i = 0; i < number_of_workers; i++) {
        workers_done_statuses.append(WorkerStatusType[kWaiting]);
    }
    auto creation_time = std::chrono::system_clock::now();
    bsoncxx::document::value doc = make_document(
        kvp("uuid", uuid),
        kvp("hash", json["hash"].asString()),
        kvp("maxLength", json["maxLength"].asInt()),
        kvp("result", JobStatusType[kInProgress]),
        kvp("passwords", bsoncxx::builder::basic::make_array()),
        kvp("number_of_workers", number_of_workers),
        kvp("workers_done_statuses", workers_done_statuses),
        kvp("created_at", bsoncxx::types::b_date{creation_time}),
        kvp("updated_at", bsoncxx::types::b_date{creation_time}));

    return doc;
}

bool HashCrack::insertInDb(
      mongocxx::collection collection,
      const mongocxx::options::insert &insert_opts,
      const bsoncxx::document::value &doc) {
    auto result = collection.insert_one(doc.view(), insert_opts);
    if (result.has_value()) {
        return true;
    } else {
        return false;
    }
}

bool HashCrack::sendTaskToWorkers(const std::string& uuid,
                                  shared_ptr<Json::Value> json_ptr) {
    boost::asio::io_context io_context;
    AMQP::LibBoostAsioHandler handler(io_context);
    AMQP::TcpConnection connection(&handler, AMQP::Address("rabbitmq", 5672, AMQP::Login("guest", "guest"), "/"));
    AMQP::TcpChannel channel(&connection);
    // auto amqpPluginPtr = app().getPlugin<AMQPClient>();
    // // auto channel = amqpPluginPtr->createChannel(kConfig["rabbitQueueName"].asString());
    // auto channel = amqpPluginPtr->createChannel("tasksQueue");


    std::mutex ack_mutex;
    std::condition_variable ack_cv;
    bool ack_received = false;
    bool nack_received = false;

    prepareAmqpChannel(&channel, uuid, ack_mutex, ack_cv,
                       ack_received, nack_received);

    declareQueueForChannel(&channel, uuid);
    std::thread io_thread([&]() {
        io_context.run();
        LOG_INFO << "Boost.Asio thread finished.";
    });
    bool send_task_status = sendTaskToRabbitQueue(
        &channel, uuid, json_ptr, ack_mutex, ack_cv,
        ack_received, nack_received);

    if (connection.usable()) {
        connection.close();
    }
    io_context.stop(); // Stop the event loop
    if (io_thread.joinable()) {
        io_thread.join();
    }
    if (!send_task_status) {
        return false;
    }
    return true;
}

void HashCrack::prepareAmqpChannel(
      AMQP::TcpChannel *channel,
      const std::string &uuid,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {
    // Enable Publisher Confirms (ACKs/NACKs)
    channel->confirmSelect()
        .onSuccess([&]() {
            LOG_INFO << "Publisher confirms enabled for task " << uuid;
        })
        .onAck([&](uint64_t deliveryTag, bool multiple) {
            LOG_INFO << "Got delivery from Rabbit on tag " << deliveryTag
                     << " for request " << uuid
                     << " and multiple is " << multiple;
            std::lock_guard lock(ack_mutex);
            ack_received = true;
            ack_cv.notify_one();
        })
        .onNack([&](uint64_t deliveryTag, bool multiple, bool requeue) {
            LOG_WARN << "Didn't get delivery from Rabbit on tag " << deliveryTag
                     << " for request " << uuid
                     << " and multiple is " << multiple;
            std::lock_guard lock(ack_mutex);
            nack_received = true;
            ack_cv.notify_one();
        })
        .onError([&](const char* message) {
            LOG_ERROR << "Error enabling publisher confirms for task " << uuid
                      << ": " << message;
            // Signal failure immediately if confirms can't be enabled
            std::lock_guard lock(ack_mutex);
            nack_received = true; // Treat as NACK
            ack_cv.notify_one();
        });
}

void HashCrack::declareQueueForChannel(
      AMQP::TcpChannel *channel,
      const std::string &uuid) {
    // std::string rabbit_queue_name = kConfig["rabbitQueueName"].asString();
    std::string rabbit_queue_name = "tasksQueue";
    channel->declareQueue(rabbit_queue_name, AMQP::durable)
        .onSuccess([](const std::string &name,
                         uint32_t messageCount,
                         uint32_t consumerCount) {
            LOG_INFO << "Queue '" << name << "' has been declared with "
                     << messageCount << " messages and "
                     << consumerCount << " consumers";
        })
        .onError([&](const char* message) {
             LOG_ERROR << "Error declaring queue '" << rabbit_queue_name
                       << "' for task " << uuid << ": " << message;
        });
}

bool HashCrack::sendTaskToRabbitQueue(
      AMQP::TcpChannel *channel,
      const std::string &uuid,
      shared_ptr<Json::Value> json_ptr,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received) {

    bool all_parts_sent = true;
    // int total_parts_count = kConfig["numberOfWorkers"].asInt();
    int total_parts_count = 4;
    for (int part = 1; part <= total_parts_count; ++part) {

        std::string message =
            buildMessageForRabbit(uuid, *json_ptr, part, total_parts_count);

        // Reset ACK flags for new message
        ack_received = false;
        nack_received = false;

        // int rabbitmq_max_retries = kConfig["rabbitMaxRetries"].asInt();
        int rabbitmq_max_retries = 3;
        // Retry logic for publishing each part
        bool part_sent = false;
        for (int attempt = 1; attempt <= rabbitmq_max_retries; ++attempt) {
             ack_received = false; // Reset before publish
             nack_received = false;

            if (!channel->ready()) {
                 LOG_WARN << "RabbitMQ channel not ready (attempt "
                          << attempt << ") for task " << uuid
                          << ", part " << part;
                 continue; // Try again
            }

            try {
                // std::string rabbitmq_queue_name =
                //     kConfig["rabbitQueueName"].asString();
                std::string rabbitmq_queue_name = "tasksQueue";
                channel->publish("", rabbitmq_queue_name, message);
                LOG_INFO << "Published part " << part << "/"
                         << total_parts_count << " for task " << uuid
                         << " (attempt " << attempt << ")";

                std::unique_lock lock(ack_mutex);
                // auto wait_duration =
                //     std::chrono::duration_cast<std::chrono::seconds>(
                //     std::chrono::seconds(kConfig["timeout"].asInt())) /
                //     (kNumberOfWorkers * total_parts_count);
                auto wait_duration =
                    std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::seconds(60)) /
                        (kNumberOfWorkers * total_parts_count);

                if (ack_cv.wait_for(lock, wait_duration, [&] {
                        return ack_received || nack_received;
                    })) {
                    if (ack_received) {
                        part_sent = true;
                        LOG_INFO << "ACK received for part " << part
                                 << "on task " << uuid;
                        break; // Part sent successfully
                    } else {
                         LOG_WARN << "NACK received for part " << part
                                  << " (attempt " << attempt << ") on task "
                                  << uuid;
                    }
                } else {
                    LOG_WARN << "Timeout waiting for ACK/NACK for part " << part
                             << " (attempt " << attempt << ") on task " << uuid;
                }

            } catch (const AMQP::Exception& e) {
                LOG_ERROR << "RabbitMQ publish exception (attempt " << attempt
                          << ") for task " << uuid << ", part " << part << ": "
                          << e.what();
                return false;
            } catch (const std::exception& e) {
                LOG_ERROR << "Generic exception during RabbitMQ publish "
                          <<"(attempt " << attempt << ") for task " << uuid
                          << ", part " << part << ": " << e.what();
                return false;
            }
        }

        if (!part_sent) {
            LOG_ERROR << "Failed to send part " << part << " for task "
                      << uuid << " after " << rabbitmq_max_retries
                      << " attempts or timeout.";
            all_parts_sent = false;
            break;
        }
    }
    return all_parts_sent;
}

std::string HashCrack::buildMessageForRabbit(
      const std::string &uuid,
      const Json::Value &json,
      const int &part,
      const int &total_parts_count) {
    // Construct JSON message for the work unit
    Json::Value work_unit_json =
        ManagerToWorkerDTO(uuid,
                           part - 1,
                           total_parts_count,
                           json["hash"].asString(),
                           json["maxLength"].asInt(),
                           Alphabet).toJson();

    Json::StreamWriterBuilder writer;
    std::string message = writeString(writer, work_unit_json);
    return message;
}

void HashCrack::makeTasksFail(const std::string &uuid) {
    auto client = app().getPlugin<MongoPlugin>()->getMongoConnection();
    try {
        auto collection = client["MD5HashCrack"]["Results"];

        auto filter = make_document(kvp("uuid", uuid));
        auto array_update = make_document(kvp("$set", make_document(
            kvp("workers_done_statuses.$[elem]", WorkerStatusType[kFailed]))));

        mongocxx::options::update opts;
        bsoncxx::array::value array_filter =
            bsoncxx::builder::basic::make_array(
                make_document(kvp("elem", WorkerStatusType[kWaiting])));
        opts.array_filters(array_filter.view());

        auto result =
            collection.update_one(filter.view(), array_update.view(), opts);
/*
 * TODO:
 * - Найти способ узнавать, сколько записей стало Failed. Если все, то ставить Error, иначе PartialResult.
 * - Скорее всего надо смотреть на статус до этого - если был Waiting, то Error ставить. Если был PartialResult, то его и оставлять.
 *
*/
        if (result && result->modified_count() > 0) {
            if (true) {
                collection.update_one(
                    filter.view(),
                    make_document(kvp("$set", make_document(
                        kvp("Result", JobStatusType[kError])))));
                LOG_INFO << "Task " << uuid << " status updated to ERROR.";
            } else {
                collection.update_one(
                    filter.view(),
                    make_document(kvp("$set", make_document(
                        kvp("Result", JobStatusType[kPartialResult])))));
                LOG_INFO << "Task " << uuid
                         << " status updated to PARTIAL_RESULT.";
            }
        } else {
            LOG_INFO << "No workers failed on " << uuid;
        }

    } catch (const std::exception& e) {
        LOG_FATAL << "Failed to update task status to ERROR for " << uuid
                  << ": " << e.what();
        throw;
    }
}

/*
Логика уведомления такая: сначала мы проходим по всем воркерам,
отправляя get запросы, чтобы убедиться, что они работают.
После того, как мы получим список работающих воркеров,
мы разобьём задачу на n частей для всех живых работяг.
и отправим им их задачи POST'ом с таймером на ответ.
event loop не забивается, т.к. запросы выполняются асинхронно и
только последний выполненный запрос начинает настоящую работу
*/

// void HashCrack::notifyWorkersOnTask(std::string &&uuid) {
//     LOG_INFO << "Checking workers for " << uuid << " request.";
//     std::vector<std::string> endpoints = readEndpointsFromFile();
//
//     std::unique_lock lock(request_store_mtx_);
//     auto request = request_store_.at(uuid);
//     lock.unlock();
//
//     auto live_endpoints = std::make_shared<std::vector<std::string>>();
//     auto remaining_requests =
//         std::make_shared<std::atomic<int>>(static_cast<int>(endpoints.size()));
//
//     for (const auto& endpoint : endpoints) {
//         auto client = HttpClient::newHttpClient(endpoint);
//         auto req = HttpRequest::newHttpRequest();
//         req->setMethod(Get);
//         req->setPath("/internal/api/worker/hash/crack/task");
//
//         double kEndpointHealthStatusTimeout = 3.0;
//         client->sendRequest(req,
//             [endpoint, live_endpoints, remaining_requests, uuid, request, this]
//             (ReqResult result, const HttpResponsePtr &response) {
//             if (result == ReqResult::Ok &&
//                 response->getStatusCode() == k200OK) {
//                 live_endpoints->push_back(endpoint);
//                 LOG_INFO << "Endpoint " << endpoint << " is up.";
//             } else {
//                 LOG_INFO << "Endpoint " << endpoint << " is down.";
//             }
//
//             // Check if all requests are completed
//             if (--(*remaining_requests) == 0) {
//                 this->sendTaskToWorkers(live_endpoints, uuid, request);
//             }
//         }, kEndpointHealthStatusTimeout);
//     }
// }

//TODO:
// - Вынести инициализацию crackResultStore_.
// - Для этого надо заранее составить список живых воркеров.

// void HashCrack::sendTaskToWorkers(
//       std::shared_ptr<std::vector<std::string>> live_endpoints,
//       const std::string &uuid,
//       const std::shared_ptr<Request> &request) {
//
//     LOG_INFO << "All health checks done. Live endpoints: "
//              << live_endpoints->size();
//
//     // Now send tasks to live endpoints
//     int part_count = static_cast<int>(live_endpoints->size());
//
//     if (part_count == 0) {
//         LOG_ERROR
//             << "All endpoints are down. "
//             << "Setting error-status to request with number " << uuid << "." ;
//         std::lock_guard lock(crack_result_store_mtx_);
//         crack_result_store_[uuid]->result["status"] = JobStatusType[kError];
//         return;
//     }
//     std::unique_lock lock(request->mtx);
//     request->live_endpoints = live_endpoints;
//     lock.unlock();
//
//     for (int part_number = 0; part_number < part_count; ++part_number) {
//         sendTaskPartToWorker(uuid, part_count, part_number,
//                              request, live_endpoints);
//     }
// }

void HashCrack::sendTaskPartToWorker(
      std::string uuid, int part_count, int part_number,
      const std::shared_ptr<Request> &request,
      std::shared_ptr<std::vector<std::string>> &live_endpoints) {
    std::unique_lock lock(crack_result_store_mtx_);
    crack_result_store_[uuid]->workers.insert({part_number, kWaiting});
    lock.unlock();

    auto liveEndpoint = live_endpoints->at(part_number);
    Json::Value json =
        ManagerToWorkerDTO(uuid,
                           part_number,
                           part_count,
                           request->request_body["hash"].asString(),
                           request->request_body["maxLength"].asInt(),
                           Alphabet).toJson();
    auto task_req = HttpRequest::newHttpJsonRequest(json);
    auto client = HttpClient::newHttpClient(liveEndpoint);
    task_req->setMethod(Post);
    task_req->setPath("/internal/api/worker/hash/crack/task");

    LOG_INFO << "Sent task part " << part_number + 1 << " out of " << part_count
             << " to endpoint " << liveEndpoint << ".";
    // double kDelayTimeout = std::stod(std::getenv("DELAY_TIMEOUT"));
    double kDelayTimeout = 600.0;
    client->sendRequest(task_req,
        [](ReqResult reqResult, const HttpResponsePtr &workerResponse) {},
        kDelayTimeout);

    //Возможен pollution, если у нас много маленьких тасок.
    app().getLoop()->runAfter(kDelayTimeout, [uuid, part_number, this]() {
        this->processWorkersRespond(uuid, part_number);
    });
}

void HashCrack::processWorkersRespond(const std::string &uuid,
                                      const int &part_number) {
    std::unique_lock lock(crack_result_store_mtx_);
    auto crack_result = crack_result_store_[uuid];
    lock.unlock();

    std::lock_guard lock_res(crack_result->mtx);
    if (crack_result->workers[part_number] == kWaiting) {
        LOG_INFO << "Timer for worker number " << part_number
                 << " on job number " << uuid << " is out.";
        crack_result->workers[part_number] = kFailed;
    }
    else {
        return;
    }
    for (auto worker : crack_result->workers) {
        if (worker.second == kWaiting || worker.second == kDone) {
            return;
        }
    }
    LOG_INFO << "Setting status ERROR to job with number " << uuid << ".";
    crack_result->result["status"] = JobStatusType[kError];
}

bool HashCrack::checkIfTimeout(shared_ptr<CrackResult> &crack_result,
                               const WorkerToManagerDTO &response) {
    if (crack_result->workers[response.getPartNumber()] == kFailed) {
        LOG_INFO
            << "Ignoring responce from worker " << response.getPartNumber()
            << " on request number " << response.getRequestId()
            << " - TIMEOUT.";
        return true;
    }
    return false;
}

std::vector<std::string> HashCrack::readEndpointsFromFile() {
    std::vector<std::string> endpoints;
    //ifstream inf(std::getenv("WORKERS_LIST"));
    ifstream inf("C:\\Users\\Contarr\\Desktop\\" \
                 "ParallelProject\\manager\\workers.txt");
    string str;
    while (getline(inf, str)) {
        endpoints.push_back(str);
    }
    inf.close();
    return endpoints;
}

void HashCrack::setProgressValue(
      std::shared_ptr<CrackResult> &crack_result,
      const std::string &request_id) {
    LOG_INFO << "Setting progress value for " << request_id << ".";

    std::unique_lock ulock(request_store_mtx_);
    if (request_store_.find(request_id) == request_store_.end()) {
        ulock.unlock();
        std::lock_guard lock(crack_result->mtx);
        crack_result->result["progress"] = "100%";
        LOG_INFO << "Progress for request " << request_id << " set up.";
        return;
    }

    auto request = request_store_[request_id];
    ulock.unlock();
    std::unique_lock request_lock(request->mtx);
    int length = request->request_body["maxLength"].asInt();
    int part_count = request->live_endpoints->size();
    request_lock.unlock();

    size_t max_iterations = 0;
    for (int i = 1; i <= length; ++i) {
        max_iterations += static_cast<size_t>(pow(Alphabet.size(), i));
    }
    size_t sum_iterations = 0;

    for (int part_number = 0; part_number < part_count; ++part_number) {
        request_lock.lock();
        std::string live_endpoint = request->live_endpoints->at(part_number);
        request_lock.unlock();
        countIterations(crack_result, request_id, live_endpoint, part_number,
                        part_count, max_iterations, sum_iterations);
    }
    std::lock_guard lock(crack_result->mtx);
    crack_result->result["progress"] =
        std::to_string(
            static_cast<int>(
                static_cast<double>(sum_iterations) /
                static_cast<double>(max_iterations) *
                100)
            )
        + '%';
    LOG_INFO << "Progress for request " << request_id << " set up.";
}

void HashCrack::countIterations(
      std::shared_ptr<CrackResult> &crack_result,
      const std::string &request_id,
      const std::string &live_endpoint,
      const int &part_number,
      const int &part_count,
      const size_t &max_iterations,
      size_t &sum_iterations) {
    std::unique_lock lock(crack_result->mtx);
    if (crack_result->workers[part_number] != kWaiting) {
        LOG_INFO << "Worker number " << part_number << " on request "
                 << request_id << " made all his work.";
        sum_iterations += max_iterations/part_count;
        lock.unlock();
        return;
    }
    lock.unlock();

    auto respJson = getIterationsFromWorker(live_endpoint, request_id,
                                            part_number);

    if (!respJson->isNull() &&
        respJson->isUInt64()) {
        LOG_INFO << "Got response from worker " << part_number
                  << " on request " << request_id << " of task progress.";
        size_t task_iterations = respJson->asUInt64();
        sum_iterations += task_iterations;
        }
    else {
        LOG_ERROR << "Worker number " << part_number
                  << " didn't found progress on request " << request_id << ".";
    }
}

std::shared_ptr<Json::Value> HashCrack::getIterationsFromWorker(
      const std::string &live_endpoint,
      const std::string &request_id,
      const int &part_number) {
    auto client = HttpClient::newHttpClient(live_endpoint);
    auto request = HttpRequest::newHttpRequest();
    request->setMethod(Get);
    request->setPath("/internal/api/worker/hash/crack/percentage");
    request->setParameter("request_id", request_id);

    LOG_INFO << "Sending request for worker " << part_number << " on request "
             << request_id << " for task progress.";

    double kIterRequestTimeout = 5.0;
    auto resp = client->sendRequest(request, kIterRequestTimeout);
    auto req_result = resp.first;
    LOG_INFO << "1";
    std::shared_ptr<Json::Value> respJson =
        make_shared<Json::Value>(Json::nullValue);
    if (req_result == ReqResult::Ok) {
        respJson = resp.second->jsonObject();
        LOG_INFO << "2";
    }
    else {
        LOG_ERROR << "Failed to get response from worker " << part_number
          << " on request " << request_id << " of task progress.";
    }
    return respJson;
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