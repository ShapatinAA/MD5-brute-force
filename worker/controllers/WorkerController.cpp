//
// Created by Contarr on 12.03.2025.
//

#include "WorkerController.h"
#include "ManagerToWorkerDTO.h"
#include "WorkerToManagerDTO.h"

#include <openssl/md5.h>
#include <cmath>

//TODO: Вынести логику отправки ответа на эндпоинт в отдельную функцию.
void WorkerController::crackInitialize(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback) {

    auto isTimeout = make_shared<atomic_bool>(false);

    app().getLoop()->runAfter(60.0, [isTimeout]() {
            *isTimeout = true;
    });

    callback(HttpResponse::newHttpResponse());

    if (!ManagerToWorkerDTO::validateJson(req->jsonObject())) {
        LOG_ERROR << "Invalid request from " << req->getPeerAddr().toIpPort() << " server.";
        auto resp = HttpResponse::newHttpResponse();
        resp -> setStatusCode(k500InternalServerError);
        callback(resp);
        return;
    }
    ManagerToWorkerDTO task(*req->jsonObject());
    LOG_INFO << "Starting to brute force hash " << task.getHash() << " from request " << task.getRequestId() << ".";

    vector<string> results = bruteForce(move(task), isTimeout);

    LOG_INFO << "Sending results to manager.";

    HttpClientPtr client = HttpClient::newHttpClient(getenv("MANAGER_ADDRESS"));
    auto answer = HttpRequest::newHttpJsonRequest(WorkerToManagerDTO(task.getRequestId(),task.getPartNumber(), results).toJson());
    answer->setMethod(Patch);
    answer->setPath("/internal/api/manager/hash/crack/request");

    client->sendRequest(answer, [](ReqResult result, const HttpResponsePtr &response) {
        if (result == ReqResult::Ok && response->getStatusCode() == 200) {
            LOG_INFO << "Successful sent results.";
        }
        else {
            LOG_ERROR << "Failure sent results.";
        }
    }, 20.0);

}

vector<string> WorkerController::bruteForce(ManagerToWorkerDTO &&task, const shared_ptr<atomic_bool> &isTimeout) {

    string hash = task.getHash();
    int partCount = task.getPartCount();
    int partNumber = task.getPartNumber();
    int maxLength = task.getMaxLength();
    int alphabetSize = static_cast<int>(task.getAlphabet().size());

    vector<string> results;

    for (int length = 1; length <= maxLength; ++length) {
        size_t total_combinations = 0;
        total_combinations = static_cast<size_t>(pow(alphabetSize, length));

        size_t start = total_combinations * partNumber / partCount;
        size_t end = total_combinations * (partNumber + 1) / partCount;


        for (size_t i = start; i < end && !(*isTimeout); ++i) {
            string candidate = "";
            size_t num = i;

            for (int j = 0; j < length; ++j) {
                candidate = task.getAlphabet()[num % alphabetSize] + candidate;
                num /= alphabetSize;
            }
            trantor::utils::Hash128 candidateMD5 = trantor::utils::md5(candidate);
            char candidateCharMD5[32 + 1]; //32 символа MD5 + \0
            for (int j = 0; j < 16; ++j) {
                snprintf(&candidateCharMD5[ j*2 ], 32, "%02x", candidateMD5.bytes[j]);
            }
            std::string candidateStrMD5(candidateCharMD5);

            if (hash.compare(candidateStrMD5) == 0) {
                LOG_INFO << "Found word!";
                results.push_back(candidate);
            }
        }
    }
    return results;
}

void WorkerController::sendStatusAlive(const HttpRequestPtr &req,
                                       std::function<void(const HttpResponsePtr &)> &&callback) {
    LOG_INFO << "Sending alive-status to manager.";
    callback(HttpResponse::newHttpResponse());
}

