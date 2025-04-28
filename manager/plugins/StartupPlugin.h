//
// Created by Contarr on 25.04.2025.
//

#pragma once

#include "CrackStatuses.h"

#include <amqpcpp.h>
#include <openssl/types.h>
#include <amqpcpp/linux_tcp/tcphandler.h>
#include <amqpcpp/linux_tcp/tcpparent.h>
#include <amqpcpp/linux_tcp/tcpconnection.h>
#include <amqpcpp/linux_tcp/tcpchannel.h>
#include <drogon/drogon.h>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <memory>
#include <string>
#include <WorkerToManagerDTO.h>
#include <boost/asio/io_context.hpp>


using namespace CrackStatuses;


class StartupPlugin : public drogon::Plugin<StartupPlugin> {
  public:
    void initAndStart(const Json::Value& config) override;
    void shutdown() override;

    void resumeWork();

    void retrieveReadyResults(mongocxx::collection &collection);

    bool declareQueueForRead(AMQP::TcpChannel &channel_ptr);

    void readAndProcessResultQueue(
        mongocxx::collection &collection,
        AMQP::TcpChannel &channel_ptr);

    bool getAndProcessMessageFromQueue(
        mongocxx::collection &collection,
        AMQP::TcpChannel &channel_ptr,
        std::mutex &queue_empty_mutex,
        std::condition_variable &queue_empty_cv,
        bool &queue_reported_empty,
        bool &got_error,
        bool &consumed);

    void processMessageFromQueue(
      const AMQP::Message& message,
      AMQP::TcpChannel &channel_ptr,
      uint64_t deliveryTag,
      mongocxx::collection &collection);

    void validateRetrieveMessage(const Json::Value &root);

    void makeJobPartDone(mongocxx::collection &collection,
                         const WorkerToManagerDTO &message);

    mongocxx::cursor getWaitingAndUndistributedJobParts(
        mongocxx::collection &collection);

    void dealWithDoc(mongocxx::collection &collection,
                     const bsoncxx::document::view &doc);

    void makeJobFail(mongocxx::collection &collection,
                     const std::string &uuid);

    bsoncxx::document::value makeFilterForFinalType(
        const std::string &uuid,
        WorkersStatus &&worker_status,
        const std::string &part_number);

    bsoncxx::document::value makeUpdateForFinalType(
        WorkersStatus &&worker_status,
        StatusCode &&status_code,
        const std::string &part_number,
        const bsoncxx::builder::basic::array &passwords);

    void updateJobStatusInDb(
        const std::string &uuid,
        mongocxx::collection &collection,
        const bsoncxx::document::value &filter_one,
        const bsoncxx::document::value &filter_all,
        const bsoncxx::document::value &update_one,
        const bsoncxx::document::value &update_all,
        const mongocxx::options::update &opts,
        StatusCode &&status);

    void sendTaskToWorkers(
        const std::string& uuid,
        const bsoncxx::document::view &doc,
        mongocxx::collection &collection);

    bool prepareAmqpChannel(
        AMQP::TcpChannel &channel_ptr,
        const std::string &uuid,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    bool declareQueueForChannel(
        AMQP::TcpChannel &channel_ptr,
        const std::string &uuid);

    void distributeTask(
        AMQP::TcpChannel &channel_ptr,
        mongocxx::collection &collection,
        const std::string &uuid,
        const bsoncxx::document::view &doc,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received);

    std::string buildMessageForRabbit(
        const std::string &uuid,
        const bsoncxx::document::view &doc,
        const int &part,
        const int &total_parts_count);

    bool trySendingPart(
        AMQP::TcpChannel &channel_ptr,
        const std::string &uuid,
        std::string message,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part);

    bool publishToRabbit(
        AMQP::TcpChannel &channel_ptr,
        const std::string &uuid,
        std::string message,
        std::mutex &ack_mutex,
        std::condition_variable &ack_cv,
        bool &ack_received,
        bool &nack_received,
        const int &part);

    void makeJobPartWaiting(mongocxx::collection &collection,
                            const std::string &uuid,
                            const int &part);

  private:
    std::string kRabbitHost;
    int kRabbitPort;
    std::string kRabbitUserName;
    std::string kRabbitPassword;
    double kUpdateTime;
    std::string kResultQueueName;
    std::string kTasksQueueName;
    double kTimeout;
    int kWorkersCount;

};
