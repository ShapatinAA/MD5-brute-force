//
// Created by Contarr on 12.03.2025.
//

#include "WorkerController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"

#include <openssl/md5.h>
#include <cmath>


//TODO: Добавить удаление таски из структуры после таймаута.
void WorkerController::crackInitialize(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {

    auto is_timeout = make_shared<atomic_bool>(false);

    double delay_timeout = std::stod(std::getenv("DELAY_TIMEOUT"));
    // double delay_timeout = 600.0;
    app().getLoop()->runAfter(delay_timeout,
        [is_timeout]() {
            *is_timeout = true;
    });

    callback(HttpResponse::newHttpResponse());

    if (!ManagerToWorkerDTO::validateJson(req->jsonObject())) {
        LOG_ERROR << "Invalid request from " << req->getPeerAddr().toIpPort();
        auto resp = HttpResponse::newHttpResponse();
        resp -> setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }
    ManagerToWorkerDTO task(*req->jsonObject());
    LOG_INFO << "Starting to brute force hash " << task.getHash()
             << " from request " << task.getRequestId() << ".";

    vector<string> results = bruteForceAllLength(task, is_timeout);

    sendResultsToManager(task, results);

    std::lock_guard lock(tasks_store_mtx_);
    tasks_store_.erase(task.getRequestId());
}

vector<string> WorkerController::bruteForceAllLength(
      const ManagerToWorkerDTO &task,
      const shared_ptr<atomic_bool> &is_timeout) {

    string hash = task.getHash();
    int part_count = task.getPartCount();
    int part_number = task.getPartNumber();
    int max_length = task.getMaxLength();
    vector<string> alphabet = task.getAlphabet();
    int alphabet_size = static_cast<int>(task.getAlphabet().size());
    string uuid = task.getRequestId();
    tasks_store_[uuid] = make_shared<atomic_size_t>(0);

    //если сделать конкурентную коллекцию,
    //то можно ассинхронно сделать bruteForEachLength
    vector<string> results;

    for (int length = 1; length <= max_length; ++length) {
        size_t total_combinations = 0;
        total_combinations = static_cast<size_t>(pow(alphabet_size, length));

        size_t start = total_combinations * part_number / part_count;
        size_t end = total_combinations * (part_number + 1) / part_count;


       bruteForceFixedLength(start, end, is_timeout, length,
                             alphabet_size, hash, uuid, alphabet, results);
    }
    return results;
}

void WorkerController::sendResultsToManager(
      const ManagerToWorkerDTO &task,
      const vector<string> &results) {
    LOG_INFO << "Sending results to manager.";

    std::string manager_address = std::getenv("MANAGER_ADDRESS");
    // std::string manager_address = "http://localhost:8848";
    HttpClientPtr client =
        HttpClient::newHttpClient(manager_address);
    auto answer = HttpRequest::newHttpJsonRequest(
        WorkerToManagerDTO(task.getRequestId(),
            task.getPartNumber(),
            results).toJson());
    answer->setMethod(Patch);
    answer->setPath("/internal/api/manager/hash/crack/request");

    client->sendRequest(answer,
            [](ReqResult result, const HttpResponsePtr &response) {
        if (result == ReqResult::Ok && response->getStatusCode() == 200) {
            LOG_INFO << "Successful sent results.";
        }
        else {
            LOG_ERROR << "Failure sent results.";
        }
    }, 5.0);
}

//TODO: Добавить увеличение счётчика итераций в tasks_store_ с механизмом защиты
void WorkerController::bruteForceFixedLength(
      const size_t &start,
      const size_t &end,
      const shared_ptr<atomic_bool> &is_timeout,
      const int &length,
      const int &alphabet_size,
      const string &hash,
      const string &uuid,
      const vector<string> &alphabet,
      vector<string> &results) {
    for (size_t i = start; i < end && !(*is_timeout); ++i) {
        string candidate = "";
        size_t num = i;

        for (int j = 0; j < length; ++j) {
            candidate = alphabet[num % alphabet_size] + candidate;
            num /= alphabet_size;
        }

        const int kMd5Length = 32;
        trantor::utils::Hash128 candidateMD5 =
            trantor::utils::md5(candidate);

        char candidate_char_md5[kMd5Length + 1]; //32 символа MD5 + \0
        for (int j = 0; j < kMd5Length/2; ++j) {
            snprintf(&candidate_char_md5[j*2],
                kMd5Length, "%02x", candidateMD5.bytes[j]);
        }
        std::string candidate_string_md5(candidate_char_md5);

        if (hash.compare(candidate_string_md5) == 0) {
            LOG_INFO << "Found word!";
            results.push_back(candidate);
        }
        ++(*(tasks_store_[uuid]));
    }
}

/*
 * Логика следующая - посылаем кол-во обработанных слов.
 * Manager в свою очередь сам высчитает сколько это добавляет процентов.
 * Прим.:
 * По истечении таймера в 60 секунд соответствующая таска из tasks_store_
 * будет удалена.
*/
void WorkerController::sendPercentage(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback,
      const std::string &request_id) {
    LOG_INFO << "Sending percentage.";
    std::lock_guard lock(tasks_store_mtx_);
    if (tasks_store_.find(request_id) != tasks_store_.end()) {
        auto res = tasks_store_[request_id];
        callback(HttpResponse::newHttpJsonResponse(Json::Value(*res)));
    }
    else {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void WorkerController::sendStatusAlive(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {
    LOG_INFO << "Sending alive-status to manager.";
    callback(HttpResponse::newHttpResponse());
}

