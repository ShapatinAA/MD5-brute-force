//
// Created by Contarr on 10.03.2025.
//

#pragma once

#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>

using namespace drogon;

class HashCrack : public HttpController<HashCrack> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HashCrack::crackInitialize, "/api/hash/crack", Post);
    ADD_METHOD_TO(HashCrack::getCrackResult, "/api/hash/status?request_id={uuid}", Get);
    ADD_METHOD_TO(HashCrack::processTaskResponde, "/internal/api/manager/hash/crack/request", Patch);
    METHOD_LIST_END

    void crackInitialize(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

    void getCrackResult(const HttpRequestPtr &req,
                        std::function<void(const HttpResponsePtr &)> &&callback,
                        const std::string &request_id);

    void processTaskResponde(const HttpRequestPtr &req,
                             std::function<void(const HttpResponsePtr &)> &&callback);

protected:

    const std::string StatusTypes[4]{"IN_PROGRESS", "READY", "ERROR", "PARTIAL_RESULT"};

    const std::vector<std::string> Alphabet{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
                                                       "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
                                                       "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
                                                       "u", "v", "w", "x", "y", "z"};

/*
 * TODO:
 *  - Заменить Json-поля во всех классах на поля с конкретными типами и именами. Классы должны представлять чёткие объекты, без возможности двояко интерпретировать Value::Json.
 *  - Добавить необходимые DTO для отправки пользователю.
*/

    enum StatusCode
    {
        IN_PROGRESS_,
        READY_,
        ERROR_,
        PARTIAL_RESULT_
    };

    enum WorkersStatus {
        DONE,
        TIMEOUT,
        WAITING
    };

    struct Request
    {
        Json::Value requestBody;
        std::mutex mtx;
    };

    struct CrackResult
    {
        Json::Value result;
        std::unordered_map<int, WorkersStatus> workers;
        std::mutex mtx;
    };

    static bool isNotMD5(const std::string &hash);

    static bool requestValidated(const std::shared_ptr<Json::Value> &reqBodyJsonPtr);

    bool addToCrackResults(const std::string &uuid);

    Json::Value addToStorageRequests(const std::string &uuid,
                                     const std::shared_ptr<Json::Value> &reqBodyJsonPtr);

    static HttpResponsePtr makeFailedResponse();

    static HttpResponsePtr makeSuccessResponse(Json::Value &&uuid);

    static std::string getRandomString(size_t n);

    void notifyWorkersOnTask(std::string &&uuid);

    void sendTasksToWorkers(const std::shared_ptr<std::vector<std::string>> &liveEndpoints,
                            const std::string &uuid,
                            const std::shared_ptr<Request> &request);

    void processWorkersRespond(const std::string &uuid,
                               const int &partNumber);


    std::unordered_map<std::string, std::shared_ptr<CrackResult>> crackResultStore_;
    std::unordered_map<std::string, std::shared_ptr<Request>> requestStore_;
    int maxStoreSize_ = 10;
    std::mutex requestStoreMtx_;
    std::mutex crackResultStoreMtx_;
};
