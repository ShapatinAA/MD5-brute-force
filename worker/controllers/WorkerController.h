//
// Created by Contarr on 12.03.2025.
//

#pragma once

#include <drogon/HttpClient.h>
#include <drogon/HttpController.h>

#include "ManagerToWorkerDTO.h"

using namespace drogon;

class WorkerController : public HttpController<WorkerController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(WorkerController::crackInitialize,
                  "/internal/api/worker/hash/crack/task",
                  Post);
    ADD_METHOD_TO(WorkerController::sendStatusAlive,
                  "/internal/api/worker/hash/crack/task",
                  Get);
    ADD_METHOD_TO(
        WorkerController::sendPercentage,
        "/internal/api/worker/hash/crack/percentage?request_id={uuid}",
        Get);
    METHOD_LIST_END

    void crackInitialize(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    static void sendStatusAlive(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    void sendPercentage(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string &request_id);

protected:

    vector<string> bruteForceAllLength(
        const ManagerToWorkerDTO &task,
        const shared_ptr<atomic_bool> &isTimeout);

    void bruteForceFixedLength(
        const size_t &start,
        const size_t &end,
        const shared_ptr<atomic_bool> &is_timeout,
        const int &length,
        const int &alphabet_size,
        const string &hash,
        const string &uuid,
        const vector<string> &alphabet,
        vector<string> &results);

    static void sendResultsToManager(const ManagerToWorkerDTO &task,
                                     const vector<string> &results);

    std::unordered_map<std::string, std::shared_ptr<std::atomic_size_t>>
        tasks_store_;
    std::mutex tasks_store_mtx_;

};

