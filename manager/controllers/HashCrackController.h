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
#include "CrackStatuses.h"

using namespace drogon;
using namespace CrackStatuses;


class HashCrack : public HttpController<HashCrack> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(HashCrack::crackInitialize, "/api/hash/crack", Post);
    ADD_METHOD_TO(HashCrack::getCrackResult,
                  "/api/hash/status?request_id={uuid}",
                  Get);
    METHOD_LIST_END

    void crackInitialize(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback);

    void getCrackResult(
        const HttpRequestPtr &req,
        std::function<void(const HttpResponsePtr &)> &&callback,
        const std::string &request_id);

    mongocxx::cursor getJobFromDb(mongocxx::collection &collection,
                                  const std::string &uuid);

protected:

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

    bool prepareAmqpChannel(
        AMQP::TcpChannel &channel,
        const string &uuid,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    bool declareQueueForChannel(
        AMQP::TcpChannel &channel,
        const std::string &uuid);

    void distributeTask(
        AMQP::TcpChannel &channel,
        const std::string &uuid,
        shared_ptr<Json::Value> json_ptr,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    bool sendTaskPartToRabbit(
        AMQP::TcpChannel &channel,
        const std::string &uuid,
        shared_ptr<Json::Value> json_ptr,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part,
        const int &total_parts_count);

    bool trySendingPart(
        AMQP::TcpChannel &channel,
        const std::string &uuid,
        std::string message,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part);

    bool publishToRabbit(
        AMQP::TcpChannel &channel,
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

    void makeJobFail(const std::string &uuid);

    void updateJobStatusInDb(const std::string &uuid,
                             mongocxx::collection &collection,
                             const bsoncxx::document::value &filter_one,
                             const bsoncxx::document::value &filter_all,
                             const bsoncxx::document::value &update_one,
                             const bsoncxx::document::value &update_all,
                             const mongocxx::options::update &opts,
                             StatusCode &&status);

    bsoncxx::document::value makeFilterForFinalType(
        const std::string &uuid,
        WorkersStatus &&worker_status,
        const std::string &part_number);

    bsoncxx::document::value makeUpdateForFinalType(
        WorkersStatus &&worker_status,
        StatusCode &&status_code,
        const std::string &part_number,
        const bsoncxx::builder::basic::array &passwords);

    static HttpResponsePtr makeFailedResponse();

    static std::string getRandomString(size_t n);
};
