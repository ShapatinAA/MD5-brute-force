//
// Created by Contarr on 10.03.2025.
//

#pragma once

#include <amqpcpp.h>
#include <openssl/types.h>
#include <amqpcpp/linux_tcp/tcphandler.h>
#include <amqpcpp/linux_tcp/tcpparent.h>
#include <amqpcpp/linux_tcp/tcpconnection.h>
#include <amqpcpp/linux_tcp/tcpchannel.h>
#include <bsoncxx/builder/stream/document.hpp>
#include <mongocxx/collection.hpp>
#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>

#include "WorkerToManagerDTO.h"

using namespace drogon;

class HashCrack : public HttpController<HashCrack> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HashCrack::crackInitialize, "/api/hash/crack", Post);
    ADD_METHOD_TO(HashCrack::getCrackResult,
                  "/api/hash/status?request_id={uuid}",
                  Get);
    ADD_METHOD_TO(HashCrack::processTaskResponde,
                  "/internal/api/manager/hash/crack/request",
                  Patch);
    METHOD_LIST_END

    void crackInitialize(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    void getCrackResult(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string &request_id);

    void processTaskResponde(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

protected:

    const std::string JobStatusType[4] {
        "IN_PROGRESS",
        "READY",
        "ERROR",
        "PARTIAL_RESULT"
    };

    const std::string WorkerStatusType[4] {
        "DONE",
        "FAILED",
        "WAITING",
        "DID_NOT_DISTRIBUTE"
    };

    const std::vector<std::string> Alphabet{
        "0", "1", "2", "3", "4", "5", "6", "7", "8",
        "9", "a", "b", "c", "d", "e", "f", "g", "h",
        "i", "j", "k", "l", "m", "n", "o", "p", "q",
        "r", "s", "t", "u", "v", "w", "x", "y", "z"};

/*
 * TODO:
 *  - Заменить Json-поля во всех классах на поля с конкретными типами и именами.
 *  - Классы должны представлять чёткие объекты, без возможности двояко
 *  - интерпретировать Value::Json.
 *  - Добавить необходимые DTO для отправки пользователю.
*/

    enum StatusCode
    {
        kInProgress,
        kReady,
        kError,
        kPartialResult
    };

    enum WorkersStatus {
        kDone,
        kFailed,
        kWaiting,
        kDidNotDistribute
    };

    struct Request
    {
        Json::Value request_body;
        std::shared_ptr<std::vector<std::string>> live_endpoints;
        std::mutex mtx;

    };

    struct CrackResult
    {
        Json::Value result;
        std::unordered_map<int, WorkersStatus> workers;
        std::mutex mtx;
    };

    static bool isNotMD5(const std::string &hash);

    static bool requestValidated(
        const std::shared_ptr<Json::Value> &req_body_json_ptr);

    bool saveTaskInDb(const std::string &uuid,
        shared_ptr<Json::Value> json_ptr);

    bsoncxx::document::value buildDocForDbInsertion(
        const std::string &uuid,
        Json::Value json);

    bool insertInDb(
        mongocxx::collection &collection, // Потенциально опасно (const!)
        const mongocxx::options::insert &insert_opts,
        const bsoncxx::document::value &doc);

    void sendTaskToWorkers(
        const std::string &uuid,
        shared_ptr<Json::Value> json_ptr);

    void prepareAmqpChannel(
        AMQP::TcpChannel *channel,
        const string &uuid,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    void declareQueueForChannel(
        AMQP::TcpChannel *channel,
        const std::string &uuid);

    void distributeTask(
        AMQP::TcpChannel *channel,
        const std::string &uuid,
        shared_ptr<Json::Value> json_ptr,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    bool sendTaskPartToRabbit(
        AMQP::TcpChannel *channel,
        const std::string &uuid,
        shared_ptr<Json::Value> json_ptr,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part,
        const int &total_parts_count);

    bool trySendingPart(
        AMQP::TcpChannel *channel,
        const std::string &uuid,
        std::string message,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part);

    bool publishToRabbit(
      AMQP::TcpChannel *channel,
      const std::string &uuid,
      std::string message,
      std::mutex &ack_mutex,
      std::condition_variable &ack_cv,
      bool &ack_received,
      bool &nack_received,
      const int &part);

    std::string buildMessageForRabbit(
        const std::string &uuid,
        const Json::Value &json,
        const int &part,
        const int &total_parts_count);

    void makeJobPartWaiting(
        const std::string &uuid,
        const int &part);

    void makeJobPartDone(const WorkerToManagerDTO &message);

    void makeJobFail(const std::string &uuid);

    void updateJobStatusInDb(const std::string &uuid,
                             mongocxx::collection* collection,
                             const bsoncxx::document::value &filter,
                             const bsoncxx::document::value &update,
                             const mongocxx::options::update &opts,
                             bool &&set_error);

    bool checkIfAllWorkersHaveType(
        mongocxx::collection* collection,
        const bsoncxx::document::value &filter,
        WorkersStatus &&worker_status);

    void setStatus(
      mongocxx::collection* collection,
      const bsoncxx::document::value &filter,
      const std::string &uuid,
      StatusCode &&job_status);










    Json::Value addToStorageRequests(
        const std::string &uuid,
        const std::shared_ptr<Json::Value> &req_body_json_ptr);

    bool checkIfTimeout(
        std::shared_ptr<CrackResult> &crack_result,
        const WorkerToManagerDTO &response);

    std::vector<std::string> readEndpointsFromFile();

    static HttpResponsePtr makeFailedResponse();

    static HttpResponsePtr makeSuccessResponse(Json::Value &&uuid);

    static std::string getRandomString(size_t n);

    void notifyWorkersOnTask(std::string &&uuid);

    void setProgressValue(
        std::shared_ptr<CrackResult> &crack_result,
        const std::string& request_id);

    void sendTaskPartToWorker(
        std::string uuid, int part_count, int part_number,
        const std::shared_ptr<Request> &request,
        std::shared_ptr<std::vector<std::string>> &live_endpoints);

    void processWorkersRespond(const std::string &uuid,
                               const int &part_number);

    void countIterations(
        std::shared_ptr<CrackResult> &crack_result,
        const std::string &request_id,
        const std::string &live_endpoint,
        const int &part_number,
        const int &part_count,
        const size_t &max_iterations,
        size_t &sum_iterations);

    std::shared_ptr<Json::Value> getIterationsFromWorker(
        const std::string &live_endpoint,
        const std::string &request_id,
        const int &part_number);

    std::unordered_map<std::string, std::shared_ptr<CrackResult>>
        crack_result_store_;
    std::unordered_map<std::string, std::shared_ptr<Request>>
        request_store_;
    // const int kMaxRequestStoreSize =
    //     std::stoi(std::getenv("MAX_QUEUE_SIZE"));
    const int kMaxRequestStoreSize = 10;
    // const int kMongoMaxRetries =
    //     std::stoi(std::getenv("MONGO_MAX_RETRIES"));
    const int kMongoMaxRetries = 3;
    // const int kNumberOfWorkers =
    //     std::stoi(std::getenv("NUMBER_OF_WORKERS"));
    const int kNumberOfWorkers = 4;
    const Json::Value kConfig = app().getCustomConfig();
    std::mutex request_store_mtx_;
    std::mutex crack_result_store_mtx_;
};
