//
// Created by Contarr on 10.03.2025.
//

#include "HashCrackController.h"

#include <fstream>

#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"

#include <regex>
#include <drogon/HttpClient.h>

#include <drogon/HttpController.h>


using namespace drogon;

/*
 * TODO:
 *  - Добавить ограничение на кол-во добавляемых в коллекцию элементов, тем самым реализовав ограниченную очередь;
 *  - для этого уже есть переменная maxStoreSize_, остаётся внедрить проверку по ней в функцию
*/
void HashCrack::crackInitialize(const HttpRequestPtr &req,
                                std::function<void(const HttpResponsePtr &)> &&callback)
{
    LOG_INFO << "Hash cracking initialization for new user on " << req->getBody() << "." ;
    auto reqBodyJsonPtr = req->jsonObject();

    if (!requestValidated(reqBodyJsonPtr))
    {
        LOG_ERROR << "Validation failed for request " << req->jsonObject()->asString() << "." ;
        callback(makeFailedResponse());
        return;
    }

    std::string uuid = getRandomString(64);
    if (!addToCrackResults(uuid)) {
        LOG_ERROR << "Uuid already exists.";
        callback(makeFailedResponse());
        return;
    }
    Json::Value json = addToStorageRequests(uuid, reqBodyJsonPtr);
    LOG_INFO << "Request with uuid" << uuid << " added to storage.";
    LOG_INFO << "Sending uuid" << uuid << " to user.";
    callback(HttpResponse::newHttpJsonResponse(std::move(json)));
    notifyWorkersOnTask(std::move(uuid));
}

void HashCrack::getCrackResult(const HttpRequestPtr &req,
                               std::function<void(const HttpResponsePtr &)> &&callback,
                               const std::string &request_id)
{
    LOG_INFO << "Getting crack result status for " << request_id << ".";
    //std::lock_guard lock(crackResultStoreMtx_); если не изменять storage, то не нужно. Поиск ассинхронен.
    if (crackResultStore_.find(request_id) == crackResultStore_.end()) {
        LOG_ERROR << "No result was found for " << request_id << ".";
        callback(makeFailedResponse());
        return;
    }

    auto crackResult = crackResultStore_[request_id];
    std::lock_guard lock(crackResult->mtx);
    LOG_INFO << "Sending crack crack result status for " << request_id << " to user.";
    callback(HttpResponse::newHttpJsonResponse(crackResult->result));
}

//TODO: Валидация запроса. Сравнение ip-адреса, с которого получили реквест, со списком адресов.
void HashCrack::processTaskResponde(const HttpRequestPtr &req,
                                    std::function<void(const HttpResponsePtr &)> &&callback)
{
    auto response = WorkerToManagerDTO(*req->jsonObject());
    LOG_INFO << "Getting responce from worker " << response.getPartNumber() << " on request number " << response.getRequestId() << ".";
    callback(HttpResponse::newHttpResponse());

    std::unique_lock lock(crackResultStoreMtx_);
    auto crackResult = crackResultStore_[response.getRequestId()];
    lock.unlock();

    if (crackResult->workers[response.getPartNumber()] == WorkersStatus::TIMEOUT) {
        LOG_INFO << "Ignoring responce from worker " << response.getPartNumber() << " on request number " << response.getRequestId() << " - TIMEOUT.";
        return;
    }

    LOG_INFO << "Changing status of " << response.getRequestId() << " and adding data." ;
    std::lock_guard lockRes(crackResult->mtx);
    crackResult->workers[response.getPartNumber()] = WorkersStatus::DONE;
    if (crackResult->result["status"] == StatusTypes[IN_PROGRESS_]) {
        crackResult->result["status"] = StatusTypes[PARTIAL_RESULT_];
    }
    for (const auto& word : response.getAnswer()) {
        crackResult->result["data"].append(word);
    }

    for (auto worker : crackResult->workers) {
        if (worker.second == WorkersStatus::WAITING || worker.second == WorkersStatus::TIMEOUT) {
            return;
        }
    }
    LOG_INFO << "Setting status READY to job with number " << response.getRequestId() << ".";
    crackResult->result["status"] = StatusTypes[READY_];
    std::lock_guard lockReq(requestStoreMtx_);
    requestStore_.erase(response.getRequestId());
    LOG_INFO << "Erased " << response.getRequestId() << " from requests store." ;

}

bool HashCrack::isNotMD5(const std::string& hash) {
    const std::regex pattern("^[a-fA-F\\d]{32}$");
    return !(std::regex_match(hash, pattern));
}

bool HashCrack::requestValidated(const std::shared_ptr<Json::Value> &reqBodyJsonPtr) {

    if (
        reqBodyJsonPtr == nullptr ||
        (*reqBodyJsonPtr)["hash"].isNull() ||
        (*reqBodyJsonPtr)["maxLength"].isNull() ||
        isNotMD5((*reqBodyJsonPtr)["hash"].asString()) ||
        !(*reqBodyJsonPtr)["maxLength"].isInt() ||
        (*reqBodyJsonPtr)["maxLength"].asInt() < 1)
    {
        return false;
    }
    return true;
}

bool HashCrack::addToCrackResults(const std::string& uuid) {
    std::lock_guard lock(crackResultStoreMtx_);
    if (crackResultStore_.find(uuid) == crackResultStore_.end()) {
        Json::Value json;
        json["status"] = StatusTypes[IN_PROGRESS_];
        json["data"] = Json::Value(Json::arrayValue);
        auto requestPtr = std::make_shared<CrackResult>();
        requestPtr->result = std::move(json);
        crackResultStore_.insert({uuid, std::move(requestPtr)});
        return true;
    }
    return false;
}

Json::Value HashCrack::addToStorageRequests(const std::string &uuid,
                                            const std::shared_ptr<Json::Value> &reqBodyJsonPtr) {
    std::lock_guard lock(requestStoreMtx_);
    Json::Value json;
    auto requestPtr = std::make_shared<Request>();
    requestPtr->requestBody = std::move(*reqBodyJsonPtr);
    requestStore_.insert({uuid, std::move(requestPtr)});
    json["requestId"] = uuid;
    return json;
}

/*
Логика уведомления такая: сначала мы проходим по всем воркерам, отправляя get запросы, чтобы убедиться, что они работают.
После того, как мы получим список работающих воркеров, мы разобьём задачу на n частей для всех живых работяг.
и отправим им их задачи POST'ом с таймером на ответ.
event loop не забивается, т.к. запросы выполняются асинхронно и только последний выполненный запрос начинает настоящую работу
*/

void HashCrack::notifyWorkersOnTask(std::string &&uuid) {
    LOG_INFO << "Checking workers for " << uuid << " request.";
    std::vector<std::string> endpoints;
    ifstream inf(std::getenv("WORKERS_LIST"));
    string str;
    while (getline(inf, str)) {
        endpoints.push_back(str);
    }
    inf.close();
    std::unique_lock lock(requestStoreMtx_);
    auto request = requestStore_.at(uuid);
    lock.unlock();

    auto liveEndpoints = std::make_shared<std::vector<std::string>>();
    auto remainingRequests = std::make_shared<std::atomic<int>>(static_cast<int>(endpoints.size()));

    for (const auto& endpoint : endpoints) {
        auto client = HttpClient::newHttpClient(endpoint);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Get);
        req->setPath("/internal/api/worker/hash/crack/task");

        client->sendRequest(req, [endpoint, liveEndpoints, remainingRequests, uuid, request, this](ReqResult result, const HttpResponsePtr &response) {
            if (result == ReqResult::Ok && response->getStatusCode() == k200OK) {
                liveEndpoints->push_back(endpoint);
                LOG_INFO << "Endpoint " << endpoint << " is up.";
            } else {
                LOG_INFO << "Endpoint " << endpoint << " is down.";
            }

            // Check if all requests are completed
            if (--(*remainingRequests) == 0) {
                this->sendTasksToWorkers(liveEndpoints, uuid, request);
            }
        }, 3.0);
    }
}

//TODO: Вынести инициализацию crackResultStore_. Для этого надо заранее составить список живых воркеров.
void HashCrack::sendTasksToWorkers(const std::shared_ptr<std::vector<std::string>> &liveEndpoints,
                                   const std::string &uuid,
                                   const std::shared_ptr<Request> &request) {

    LOG_INFO << "All health checks done. Live endpoints: " << liveEndpoints->data();

    // Now send tasks to live endpoints
    int partCount = static_cast<int>(liveEndpoints->size());

    if (partCount == 0) {
        LOG_ERROR << "All endpoints are down. Setting error-status to request with number " << uuid << "." ;
        std::lock_guard lock(crackResultStoreMtx_);
        crackResultStore_[uuid]->result["status"] = StatusTypes[ERROR_];
        return;
    }

    for (int partNumber = 0; partNumber < partCount; ++partNumber) {
        //Initializing workers statuses in result.
        std::unique_lock lock(crackResultStoreMtx_);
        crackResultStore_[uuid]->workers.insert({partNumber, WorkersStatus::WAITING});
        lock.unlock();

        auto liveEndpoint = liveEndpoints->at(partNumber);
        Json::Value json = ManagerToWorkerDTO(uuid,
                                                 partNumber,
                                                 partCount,
                                                 request->requestBody["hash"].asString(),
                                                 request->requestBody["maxLength"].asInt(),
                                                 Alphabet).toJson();
        auto taskReq = HttpRequest::newHttpJsonRequest(json);
        auto client = HttpClient::newHttpClient(liveEndpoint);
        taskReq->setMethod(Post);
        taskReq->setPath("/internal/api/worker/hash/crack/task");
        LOG_INFO << "Sent task number " << partNumber << " out of " << partCount << " to endpoint " << liveEndpoint << ".";
        client->sendRequest(taskReq, [](ReqResult reqResult, const HttpResponsePtr &workerResponse) {}, 60);
        app().getLoop()->runAfter(60.0, [uuid, partNumber, this]() { //Возможен pollution, если у нас много маленьких тасок.
            this->processWorkersRespond(uuid, partNumber);
        });
    }
}

void HashCrack::processWorkersRespond(const std::string &uuid,
                                      const int &partNumber) {
    std::unique_lock lock(crackResultStoreMtx_);
    auto crackResult = crackResultStore_[uuid];
    lock.unlock();

    std::lock_guard lockRes(crackResult->mtx);
    if (crackResult->workers[partNumber] == WorkersStatus::WAITING) {
        LOG_INFO << "Timer for worker number " << partNumber << " on job number " << uuid << " is out.";
        crackResult->workers[partNumber] = WorkersStatus::TIMEOUT;
    }
    else {
        return;
    }
    for (auto worker : crackResult->workers) {
        if (worker.second == WorkersStatus::WAITING || worker.second == WorkersStatus::DONE) {
            return;
        }
    }
    LOG_INFO << "Setting status ERROR to job with number " << uuid << ".";
    crackResult->result["status"] = StatusTypes[ERROR_];
}

HttpResponsePtr HashCrack::makeFailedResponse()
{
    Json::Value json;
    json["error"] = true;
    auto resp = HttpResponse::newHttpJsonResponse(json);
    resp->setStatusCode(k500InternalServerError);
    return resp;
}

std::string HashCrack::getRandomString(size_t n)
{
    std::vector<unsigned char> random(n);
    utils::secureRandomBytes(random.data(), random.size());

    // This is cryptographically safe as 256 mod 16 == 0
    static const std::string alphabets = "0123456789abcdefghkjklmnopqrstuv";
    assert(256 % alphabets.size() == 0);
    std::string randomString(n, '\0');
    for (size_t i = 0; i < n; i++)
        randomString[i] = alphabets[random[i] % alphabets.size()];
    return randomString;
}