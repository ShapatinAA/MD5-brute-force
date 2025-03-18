//
// Created by Contarr on 12.03.2025.
//

#include "WorkerController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"

#include <openssl/md5.h>
#include <cmath>


//TODO: Вынести логику отправки ответа на эндпоинт в отдельную функцию.
void WorkerController::crackInitialize(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {

    auto is_timeout = make_shared<atomic_bool>(false);

    double delay_timeout = std::stod(std::getenv("DELAY_TIMEOUT"));
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

    vector<string> results = bruteForce(move(task), is_timeout);

    sendResultsToManager(task, results);

}

vector<string> WorkerController::bruteForce(
      ManagerToWorkerDTO &&task,
      const shared_ptr<atomic_bool> &is_timeout) {

    string hash = task.getHash();
    int part_count = task.getPartCount();
    int part_number = task.getPartNumber();
    int max_length = task.getMaxLength();
    int alphabet_size = static_cast<int>(task.getAlphabet().size());

    vector<string> results;

    for (int length = 1; length <= max_length; ++length) {
        size_t total_combinations = 0;
        total_combinations = static_cast<size_t>(pow(alphabet_size, length));

        size_t start = total_combinations * part_number / part_count;
        size_t end = total_combinations * (part_number + 1) / part_count;


        for (size_t i = start; i < end && !(*is_timeout); ++i) {
            string candidate = "";
            size_t num = i;

            for (int j = 0; j < length; ++j) {
                candidate = task.getAlphabet()[num % alphabet_size] + candidate;
                num /= alphabet_size;
            }
            int md5_length = 32;
            trantor::utils::Hash128 candidateMD5 =
                trantor::utils::md5(candidate);
            char candidate_char_md5[md5_length + 1]; //32 символа MD5 + \0
            for (int j = 0; j < md5_length/2; ++j) {
                snprintf(&candidate_char_md5[j*2],
                    md5_length, "%02x", candidateMD5.bytes[j]);
            }
            std::string candidate_string_md5(candidate_char_md5);

            if (hash.compare(candidate_string_md5) == 0) {
                LOG_INFO << "Found word!";
                results.push_back(candidate);
            }
        }
    }
    return results;
}

void WorkerController::sendResultsToManager(
      const ManagerToWorkerDTO &task,
      const vector<string> &results) {
    LOG_INFO << "Sending results to manager.";

    std::string manager_address = std::getenv("MANAGER_ADDRESS");
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

void WorkerController::sendStatusAlive(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback) {
    LOG_INFO << "Sending alive-status to manager.";
    callback(HttpResponse::newHttpResponse());
}

