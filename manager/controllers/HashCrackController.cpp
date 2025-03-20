//
// Created by Contarr on 10.03.2025.
//

#include "HashCrackController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"

#include <fstream>
#include <regex>
#include <drogon/HttpClient.h>
#include <cmath>

#include <drogon/HttpController.h>


using namespace drogon;

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
    if (!addToCrackResults(uuid)) {
        LOG_ERROR << "Can not add uuid.";
        callback(makeFailedResponse());
        return;
    }
    Json::Value json = addToStorageRequests(uuid, req_body_json_ptr);
    LOG_INFO << "Request with uuid" << uuid << " added to storage.";
    LOG_INFO << "Sending uuid" << uuid << " to user.";
    callback(HttpResponse::newHttpJsonResponse(std::move(json)));
    notifyWorkersOnTask(std::move(uuid));
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
    callback(HttpResponse::newHttpJsonResponse(crack_result->result));
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
    if (crack_result->result["status"] == StatusTypes[kInProgress]) {
        crack_result->result["status"] = StatusTypes[kPartialResult];
    }
    for (const auto& word : response.getAnswer()) {
        crack_result->result["data"].append(word);
    }

    for (auto worker : crack_result->workers) {
        if (worker.second == kWaiting ||
            worker.second == kTimeout) {
            return;
        }
    }
    LOG_INFO << "Setting status READY to job with number "
             << response.getRequestId() << ".";
    crack_result->result["status"] = StatusTypes[kReady];
    std::lock_guard lock_req(request_store_mtx_);
    request_store_.erase(response.getRequestId());
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

bool HashCrack::addToCrackResults(const std::string& uuid) {
    std::lock_guard lock(crack_result_store_mtx_);
    if (crack_result_store_.find(uuid) == crack_result_store_.end() &&
        crack_result_store_.size() < kMaxRequestStoreSize) {
        Json::Value json;
        json["status"] = StatusTypes[kInProgress];
        json["data"] = Json::Value(Json::arrayValue);
        json["progress"] = "0%";
        auto request_ptr = std::make_shared<CrackResult>();
        request_ptr->result = std::move(json);
        crack_result_store_.insert({uuid, std::move(request_ptr)});
        return true;
    }
    return false;
}

Json::Value HashCrack::addToStorageRequests(
      const std::string &uuid,
      const std::shared_ptr<Json::Value> &req_body_json_ptr) {
    std::lock_guard lock(request_store_mtx_);
    Json::Value json;
    auto request_ptr = std::make_shared<Request>();
    request_ptr->request_body = std::move(*req_body_json_ptr);
    request_store_.insert({uuid, std::move(request_ptr)});
    json["requestId"] = uuid;
    return json;
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

void HashCrack::notifyWorkersOnTask(std::string &&uuid) {
    LOG_INFO << "Checking workers for " << uuid << " request.";
    std::vector<std::string> endpoints = readEndpointsFromFile();

    std::unique_lock lock(request_store_mtx_);
    auto request = request_store_.at(uuid);
    lock.unlock();

    auto live_endpoints = std::make_shared<std::vector<std::string>>();
    auto remaining_requests =
        std::make_shared<std::atomic<int>>(static_cast<int>(endpoints.size()));

    for (const auto& endpoint : endpoints) {
        auto client = HttpClient::newHttpClient(endpoint);
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Get);
        req->setPath("/internal/api/worker/hash/crack/task");

        double kEndpointHealthStatusTimeout = 3.0;
        client->sendRequest(req,
            [endpoint, live_endpoints, remaining_requests, uuid, request, this]
            (ReqResult result, const HttpResponsePtr &response) {
            if (result == ReqResult::Ok &&
                response->getStatusCode() == k200OK) {
                live_endpoints->push_back(endpoint);
                LOG_INFO << "Endpoint " << endpoint << " is up.";
            } else {
                LOG_INFO << "Endpoint " << endpoint << " is down.";
            }

            // Check if all requests are completed
            if (--(*remaining_requests) == 0) {
                this->sendTaskToWorkers(live_endpoints, uuid, request);
            }
        }, kEndpointHealthStatusTimeout);
    }
}

//TODO:
// - Вынести инициализацию crackResultStore_.
// - Для этого надо заранее составить список живых воркеров.
void HashCrack::sendTaskToWorkers(
      std::shared_ptr<std::vector<std::string>> live_endpoints,
      const std::string &uuid,
      const std::shared_ptr<Request> &request) {

    LOG_INFO << "All health checks done. Live endpoints: "
             << live_endpoints->size();

    // Now send tasks to live endpoints
    int part_count = static_cast<int>(live_endpoints->size());

    if (part_count == 0) {
        LOG_ERROR
            << "All endpoints are down. "
            << "Setting error-status to request with number " << uuid << "." ;
        std::lock_guard lock(crack_result_store_mtx_);
        crack_result_store_[uuid]->result["status"] = StatusTypes[kError];
        return;
    }
    std::unique_lock lock(request->mtx);
    request->live_endpoints = live_endpoints;
    lock.unlock();

    for (int part_number = 0; part_number < part_count; ++part_number) {
        sendTaskPartToWorker(uuid, part_count, part_number,
                             request, live_endpoints);
    }
}

void HashCrack::sendTaskPartToWorker(
      std::string uuid, int part_count, int part_number,
      const std::shared_ptr<Request> &request,
      const std::shared_ptr<std::vector<std::string>> &live_endpoints) {
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
    double kDelayTimeout = std::stod(std::getenv("DELAY_TIMEOUT"));
    // double delay_timeout = 600.0;
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
        crack_result->workers[part_number] = kTimeout;
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
    crack_result->result["status"] = StatusTypes[kError];
}

bool HashCrack::checkIfTimeout(const shared_ptr<CrackResult> &crack_result,
                               const WorkerToManagerDTO &response) {
    if (crack_result->workers[response.getPartNumber()] == kTimeout) {
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
    ifstream inf(std::getenv("WORKERS_LIST"));
    string str;
    while (getline(inf, str)) {
        endpoints.push_back(str);
    }
    inf.close();
    return endpoints;
}

void HashCrack::setProgressValue(
      const std::shared_ptr<CrackResult> &crack_result,
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
      const std::shared_ptr<CrackResult> &crack_result,
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
    std::shared_ptr<Json::Value> respJson =
        make_shared<Json::Value>(Json::nullValue);
    if (req_result == ReqResult::Ok) {
        respJson = resp.second->jsonObject();
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