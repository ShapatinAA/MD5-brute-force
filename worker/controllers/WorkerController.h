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
    ADD_METHOD_TO(WorkerController::crackInitialize, "/internal/api/worker/hash/crack/task", Post);
    ADD_METHOD_TO(WorkerController::sendStatusAlive, "/internal/api/worker/hash/crack/task", Get);
    METHOD_LIST_END

    static void crackInitialize(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

    static void sendStatusAlive(const HttpRequestPtr &req,
                         std::function<void(const HttpResponsePtr &)> &&callback);

protected:

    static vector<string> bruteForce(ManagerToWorkerDTO &&task,
                                     const shared_ptr<atomic_bool> &isTimeout);

};

