//
// Created by Contarr on 28.04.2025.
//
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <memory> // For unique_ptr
#include <iomanip> // For string stream formatting
#include <fstream>

// AMQP/Boost
#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/steady_timer.hpp> // If needed for timeouts

// JSON Parsing
#include <json/json.h> // Or your preferred JSON library

// MD5 Calculation
#include <ManagerToWorkerDTO.h>
#include <openssl/md5.h>
#include <trantor/utils/Logger.h>


class Worker {

  public:
    Worker();
    ~Worker();

    void run();

    void stop();

private:
    void setupPublisherConfirms();
    void setupQueuesAndConsumer();
    void onTaskReceived(const AMQP::Message& message, uint64_t deliveryTag,
                        bool redelivered);
    void processTaskInWorkerThread(const std::string& body,
                                   uint64_t deliveryTag);
    bool publishResultAndWaitForConfirm(const std::string& result_body);

    std::vector<std::string> runBruteForceTask(
        const std::string &target_hash,
        const int &part_number,             // Part index (e.g., 0 to N-1)
        const int &total_parts_count,       // Total number of parts
        const int &max_length,
        const std::vector<std::string> &alphabet_str, // Alphabet as a single string
        std::atomic<bool>& is_timeout);

    void bruteForceFixedLength(
        const size_t &start, const size_t &end, std::atomic<bool>& is_timeout,
        const int &length, const int &alphabet_size,
        const std::string& target_hash,
        const std::vector<std::string>& alphabet,
        std::vector<std::string>& results);

    bool validateTask(const int &alphabet_size,
        const int &total_parts_count, const int &part_number);
    void validateRetrieveMessage(const Json::Value &root);
    void setTimeoutThread(std::atomic<bool> &is_timeout,
                          std::thread &timeout_thread,
                          std::atomic<bool> &stop_watcher,
                          std::mutex &watcher_mutex,
                          std::condition_variable &watcher_cv,
                          const chrono::time_point<chrono::system_clock> &dl,
                          const ManagerToWorkerDTO &task);

    void joinTimeoutThread(std::thread &timeout_thread,
                       std::atomic<bool> &stop_watcher,
                       std::condition_variable &watcher_cv,
                       ManagerToWorkerDTO &task);

    void processResult(const ManagerToWorkerDTO &task,
                       const std::vector<std::string> &results,
                       const uint64_t &delivery_tag,
                       std::atomic<bool> &is_timeout);
    void acknowledgeTask(uint64_t deliveryTag, const std::string& task_uuid,
                         int part_number);

    std::string calculate_md5(const std::string& input);

    void tryRun();



  private:
    std::string kRabbitHost;
    int kRabbitPort;
    std::string kRabbitUserName;
    std::string kRabbitPassword;
    std::string kTaskQueueName; // Queue to consume tasks from
    std::string kResultQueueName; // Queue to publish results to
    int kTimeout; // Max time allowed per task part
    const int kMd5Length = 32;

    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<AMQP::LibBoostAsioHandler> handler_;
    std::unique_ptr<AMQP::TcpConnection> connection_;
    std::unique_ptr<AMQP::TcpChannel> channel_;
    std::atomic<bool> running_{false};

    std::unique_ptr<boost::asio::thread_pool> thread_pool_;
    int pool_size_ = std::thread::hardware_concurrency() / 2;
    // int pool_size_ = 1;

    std::mutex g_publish_serializer_mutex;
    std::mutex publish_confirm_mutex_;
    std::condition_variable publish_confirm_cv_;

    bool current_publish_confirmed_ = false;
    bool current_publish_nacked_ = false;
};

inline Worker::Worker() {
    std::ifstream config_file("config.json", std::ifstream::binary);
    Json::Value config;
    config_file >> config;
    kRabbitHost = config["rabbit_host"].asString();
    kRabbitPort = config["rabbit_port"].asInt();
    kRabbitUserName = config["rabbit_user"].asString();
    kRabbitPassword = config["rabbit_password"].asString();
    kResultQueueName = config["results_queue_name"].asString();
    kTaskQueueName = config["tasks_queue_name"].asString();
    kTimeout = config["timeout"].asDouble();
};

inline Worker::~Worker() {
    stop();
    LOG_INFO << "Worker stopped.";
}


