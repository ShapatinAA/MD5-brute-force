//
// Created by Contarr on 10.03.2025.
//

#pragma once

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

    const std::string StatusTypes[4] {
        "IN_PROGRESS",
        "READY",
        "ERROR",
        "PARTIAL_RESULT"
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
        kTimeout,
        kWaiting
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

    bool addToCrackResults(const std::string &uuid);

    Json::Value addToStorageRequests(
        const std::string &uuid,
        const std::shared_ptr<Json::Value> &req_body_json_ptr);

    bool checkIfTimeout(
        const std::shared_ptr<CrackResult> &crack_result,
        const WorkerToManagerDTO &response);

    std::vector<std::string> readEndpointsFromFile();

    static HttpResponsePtr makeFailedResponse();

    static HttpResponsePtr makeSuccessResponse(Json::Value &&uuid);

    static std::string getRandomString(size_t n);

    void notifyWorkersOnTask(std::string &&uuid);

    void setProgressValue(
        const std::shared_ptr<CrackResult> &crack_result,
        const std::string& request_id);

    void sendTaskToWorkers(
        std::shared_ptr<std::vector<std::string>> live_endpoints,
        const std::string &uuid,
        const std::shared_ptr<Request> &request);

    void sendTaskPartToWorker(
        std::string uuid, int part_count, int part_number,
        const std::shared_ptr<Request> &request,
        const std::shared_ptr<std::vector<std::string>> &live_endpoints);

    void processWorkersRespond(const std::string &uuid,
                               const int &part_number);

    void countIterations(
        const std::shared_ptr<CrackResult> &crack_result,
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
    const int max_store_size_ = std::stoi(std::getenv("MAX_QUEUE_SIZE"));
    // const int max_store_size_ = 10;
    std::mutex request_store_mtx_;
    std::mutex crack_result_store_mtx_;
};
