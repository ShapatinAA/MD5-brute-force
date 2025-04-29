//
// Created by Contarr on 28.04.2025.
//

#include "Worker.h"

#include <future>
#include <ManagerToWorkerDTO.h>
#include <WorkerToManagerDTO.h>
#include <trantor/utils/Logger.h>
#include <trantor/utils/Utilities.h>

using namespace chrono;

std::vector<std::string> Worker::runBruteForceTask(
      const std::string &target_hash,
      const int &part_number,
      const int &total_parts_count,
      const int &max_length,
      const std::vector<std::string> &alphabet,
      std::atomic<bool>& is_timeout) {
    std::vector<std::string> results;
    int alphabet_size = static_cast<int>(alphabet.size());

    if (!validateTask(alphabet_size, total_parts_count, part_number)) {
        LOG_ERROR << "Error in running brute force, couldn't validate task";
        return results;
    }

    LOG_INFO << "Starting brute-force for hash" << target_hash << " part "
             << part_number << "/" << total_parts_count << ", max length "
             << max_length;

    for (int length = 1; length <= max_length && !is_timeout.load(); ++length) {
        uint64_t total_combinations = 0;
        total_combinations = static_cast<size_t>(pow(alphabet_size, length));

        size_t start = total_combinations * part_number / total_parts_count;
        size_t end = total_combinations * (part_number + 1) / total_parts_count;

        LOG_INFO << "Length " << length << ": combinations "
                 << total_combinations << ", processing range [" << start
                 << ", " << end << ")";

        bruteForceFixedLength(start, end, is_timeout, length,
                              alphabet_size, target_hash, alphabet, results);
    }

    if (is_timeout.load()) {
        LOG_INFO << "Task timed out.";
    }
    LOG_INFO << "Brute-force for part " << part_number
    << " finished. Found " << results.size() << " results.";
    return results;
}

bool Worker::validateTask(const int &alphabet_size,
      const int &total_parts_count, const int &part_number) {
    if (alphabet_size == 0) {
        LOG_ERROR << "Error: Alphabet is empty.";
        return false;
    }
    if (total_parts_count <= 0) {
        LOG_ERROR << "Error: totalPartsCount must be positive.";
        return false;
    }
    if (part_number < 0 || part_number >= total_parts_count) {
        LOG_ERROR << "Error: partNumber is out of range.";
        return false;
    }
    return true;
}

void Worker::bruteForceFixedLength(
      const size_t &start, const size_t &end, std::atomic<bool>& is_timeout,
      const int &length, const int &alphabet_size,
      const std::string& target_hash, const std::vector<std::string>& alphabet,
      std::vector<std::string>& results) {
    for (size_t i = start; i < end; ++i)
    {
        if (is_timeout.load()) {
            return; // Exit if timeout occurred
        }

        std::string candidate = "";
        size_t num = i;

        for (int j = 0; j < length; ++j) {
            candidate = alphabet[num % alphabet_size] + candidate;
            num /= alphabet_size;
        }

        std::string candidate_md5 = calculate_md5(candidate);

        if (target_hash.compare(candidate_md5) == 0) {
            LOG_INFO << "Found match: " << candidate << " (Hash: "
                     << candidate_md5 << ")";;
            results.push_back(candidate);
        }
    }
}

std::string Worker::calculate_md5(const std::string& input) {
    trantor::utils::Hash128 candidateMD5 =
            trantor::utils::md5(input);

    char candidate_char_md5[kMd5Length + 1]; //32 символа MD5 + \0
    for (int j = 0; j < kMd5Length/2; ++j) {
        snprintf(&candidate_char_md5[j*2],
            kMd5Length, "%02x", candidateMD5.bytes[j]);
    }
    std::string candidate_string_md5(candidate_char_md5);
    return candidate_string_md5;
}

void Worker::run() {
        if (running_.exchange(true)) return;

        LOG_INFO << "Worker starting...";

        thread_pool_ = std::make_unique<boost::asio::thread_pool>(pool_size_);

        while (true) {

            tryRun();

            channel_.reset();
            connection_.reset();
            handler_.reset();
            io_context_.reset();

            LOG_WARN << "Waiting 5 seconds before reconnect attempt...";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
}

void Worker::tryRun() {
    try {
        io_context_ = std::make_unique<boost::asio::io_context>();
        handler_ =
            std::make_unique<AMQP::LibBoostAsioHandler>(*io_context_);

        AMQP::Address address(kRabbitHost, kRabbitPort,
            AMQP::Login(kRabbitUserName, kRabbitPassword), "/");
        connection_ = std::make_unique<AMQP::TcpConnection>(
            handler_.get(), address);
        channel_ = std::make_unique<AMQP::TcpChannel>(
            connection_.get());

        setupPublisherConfirms();
        channel_->onError([this](const char* message) {
            LOG_ERROR << "Channel Error: " << message
                      << ". Attempting reconnect.";
            if (io_context_) io_context_->stop();
        });
        setupQueuesAndConsumer(); // Declares queues, sets QoS, starts consume

        LOG_INFO << "Worker connected. Event loop starting.";
        io_context_->run(); // Run I/O operations for AMQP
        LOG_INFO << "Worker event loop finished.";

    } catch (const std::exception& e) {
        LOG_ERROR << "Exception during worker setup/run: " << e.what();
    }
}

void Worker::stop() {
    if (!running_.exchange(false)) return;
    LOG_INFO << "Stop requested.";
    if (io_context_) io_context_->stop();
    if(thread_pool_) {
        LOG_INFO << "Stopping thread pool...";
        thread_pool_->stop();
        thread_pool_->join();
        thread_pool_.reset();
        LOG_INFO << "Thread pool stopped.";
    }
}

void Worker::setupPublisherConfirms() {
    channel_->confirmSelect()
        .onSuccess([this]() {
            LOG_INFO << "Publisher confirms enabled.";
        })
        .onAck([this](uint64_t, bool) {
            // Signal the waiting worker thread about successful publish
            std::lock_guard lock(publish_confirm_mutex_);
            current_publish_confirmed_ = true;
            current_publish_nacked_ = false; // Ensure nack is false
            publish_confirm_cv_.notify_one();
             LOG_INFO << "Result publish confirmed callback.";
        })
        .onNack([this](uint64_t, bool, bool) {
            // Signal the waiting worker thread about failed publish
            std::lock_guard lock(publish_confirm_mutex_);
            current_publish_confirmed_ = false;
            current_publish_nacked_ = true;
            publish_confirm_cv_.notify_one();
             LOG_ERROR << "Result publish NACKed callback.";
        })
       .onError([this](const char* message) {
            LOG_ERROR << "Error during publisher confirms: " << message;
            std::lock_guard lock(publish_confirm_mutex_);
            current_publish_confirmed_ = false;
            current_publish_nacked_ = true; // Treat error as NACK
            publish_confirm_cv_.notify_one();
       });
}

void Worker::setupQueuesAndConsumer() {
    channel_->declareQueue(kResultQueueName, AMQP::durable);
    channel_->declareQueue(kTaskQueueName, AMQP::durable)
        .onSuccess([this](const std::string& name, uint32_t, uint32_t) {
            LOG_INFO << "Task queue " << name << " declared successfully.";
            channel_->setQos(pool_size_)
                .onSuccess([this]() {
                    LOG_INFO << "QoS set to " << pool_size_ << ".";
                    // Start consuming from the TASK queue
                    channel_->consume(kTaskQueueName)
                        .onReceived([this](const AMQP::Message& message,
                            uint64_t deliveryTag, bool) {
                            std::string body_copy(message.body(),
                                message.bodySize()); // Copy body
                            LOG_INFO << "Dispatching task, tag: "
                                     << deliveryTag << " to worker thread.";
                            boost::asio::post(*thread_pool_,
                                [this, body = std::move(body_copy),
                                deliveryTag]() {
                                    processTaskInWorkerThread(body,
                                                              deliveryTag);
                            });
                        })
                        .onError([](const char* message) {
                            LOG_ERROR << "Consumer Error: " << message;
                        });
                })
                .onError([](const char* message){
                     LOG_ERROR << "Failed to set QoS: " << message;
                     throw std::runtime_error("Failed to set QoS");
                });
        })
        .onError([this](const char* message) {
            LOG_ERROR << "Failed to declare task queue '"
                      << kTaskQueueName << "': " << message;
            throw std::runtime_error("Failed to declare task queue");
        });
}

void Worker::processTaskInWorkerThread(const std::string& body,
                                       uint64_t deliveryTag) {
    LOG_INFO << "Worker thread processing task (tag " << deliveryTag << ")";
    ManagerToWorkerDTO task;

    try {
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(body, root)) {
            throw std::runtime_error("Failed to parse task JSON: "
                + reader.getFormattedErrorMessages());
        }
        validateRetrieveMessage(root);
        task = ManagerToWorkerDTO(root);

        auto deadline = system_clock::now() + seconds(kTimeout);

        std::atomic<bool> is_timeout{false};
        std::thread timeout_thread;
        std::atomic<bool> stop_watcher{false};
        std::mutex watcher_mutex;
        std::condition_variable watcher_cv;

        setTimeoutThread(is_timeout, timeout_thread, stop_watcher,
                         watcher_mutex, watcher_cv, deadline, task);

        std::vector<std::string> results = runBruteForceTask(task.getHash(),
            task.getPartNumber(), task.getPartCount(), task.getMaxLength(),
            task.getAlphabet(), is_timeout);

        joinTimeoutThread(timeout_thread, stop_watcher, watcher_cv, task);

        processResult(task, results, deliveryTag, is_timeout);

    } catch (const std::exception& e) {
        LOG_ERROR << "Worker thread: Error processing task (tag "
                  << deliveryTag << ", UUID: " << task.getRequestId() << "): "
                  << e.what();
    }
    LOG_INFO << "Worker thread finished processing task tag " << deliveryTag;
}

void Worker::validateRetrieveMessage(const Json::Value& root) {
    if (!root.isMember("RequestId") || !root["RequestId"].isString() ||
                !root.isMember("PartNumber") || !root["PartNumber"].isInt() ||
                !root.isMember("PartCount") ||
                !root["PartCount"].isInt() || !root.isMember("Hash") ||
                !root["Hash"].isString() || !root.isMember("MaxLength") ||
                !root["MaxLength"].isInt() || !root.isMember("Alphabet") ||
                !root["Alphabet"].isArray()) {
        throw std::runtime_error("Missing or invalid fields in task JSON");
    }
}

void Worker::setTimeoutThread(std::atomic<bool> &is_timeout,
                              std::thread &timeout_thread,
                              std::atomic<bool> &stop_watcher,
                              std::mutex &watcher_mutex,
                              std::condition_variable &watcher_cv,
                              const time_point<system_clock> &dl,
                              const ManagerToWorkerDTO &task) {
    timeout_thread = std::thread([&]() {
        auto now = system_clock::now();
        if (now < dl) {
            auto wait_duration = dl - now;
            std::unique_lock lock(watcher_mutex);
            if (!watcher_cv.wait_for(lock, wait_duration, [&stop_watcher]
                {return stop_watcher.load();})) {
                is_timeout = true;
                LOG_INFO << "Timeout occurred for task "
                         << task.getRequestId() << ", part "
                         << task.getPartNumber();
            };
        } else {
            LOG_INFO << "Task already timed out before starting: "
                     << task.getRequestId() << ", part "
                     << task.getPartNumber();
        }
    });
}

void Worker::joinTimeoutThread(std::thread &timeout_thread,
                       std::atomic<bool> &stop_watcher,
                       std::condition_variable &watcher_cv,
                       ManagerToWorkerDTO &task) {
    if (timeout_thread.joinable()) {
        stop_watcher = true;
        watcher_cv.notify_one();
        timeout_thread.join();
        LOG_INFO << "Timeout watcher thread joined for task "
                 << task.getRequestId();
    }
}

void Worker::processResult(const ManagerToWorkerDTO &task,
                           const std::vector<std::string> &results,
                           const uint64_t &delivery_tag,
                           std::atomic<bool> &is_timeout) {
    Json::Value result_json = WorkerToManagerDTO(task.getRequestId(),
        task.getPartNumber(), results)
        .toJson();
    Json::StreamWriterBuilder writer;
    std::string result_body = Json::writeString(writer, result_json);

    bool published_ok = publishResultAndWaitForConfirm(result_body);

    std::string task_uuid = task.getRequestId();
    int part_number = task.getPartNumber();
    if (published_ok) {
        boost::asio::post(*io_context_, [this, delivery_tag, task_uuid,
                                                  part_number]() {
            acknowledgeTask(delivery_tag, task_uuid, part_number);
        });
    } else {
        LOG_ERROR << "Worker thread: Failed to publish result for " << task_uuid
                  << ", part " << part_number << ". Task will be redelivered.";
    }
}

bool Worker::publishResultAndWaitForConfirm(const std::string& result_body) {
    std::lock_guard serializable_lock(g_publish_serializer_mutex);
    if (!io_context_) {
        LOG_ERROR << "Worker thread: Cannot publish result " \
                     "- IO context missing.";
        return false;
    }
    bool publish_sent_ok = false;
    AMQP::Envelope envelope(result_body);
    envelope.setDeliveryMode(2);
    if (channel_ && channel_->usable()) {
         LOG_INFO << "IO Thread: Attempting to publish result...";
         publish_sent_ok = channel_->publish("", kResultQueueName, envelope);
         if(publish_sent_ok) {
             LOG_INFO << "IO Thread: Publish command sent ok, awaiting " \
                          "confirmation callback...";
         } else {
             LOG_ERROR << "IO Thread: channel->publish call failed" \
                         " immediately for result.";
         }
    } else {
         LOG_ERROR << "IO Thread: Cannot publish result - Channel not usable.";
    }
    if (!publish_sent_ok) {
        LOG_ERROR << "Worker thread: Publish command failed to send.";
        return false;
    }
    LOG_INFO << "Worker thread: Waiting for publish confirmation callback...";
    std::unique_lock lock(publish_confirm_mutex_);
    if (publish_confirm_cv_.wait_for(lock, seconds(10), [&] {
        return current_publish_confirmed_ || current_publish_nacked_; })) {
        bool success = current_publish_confirmed_ && !current_publish_nacked_;
        LOG_INFO << "Worker thread: Publish confirmation result: "
                 << (success ? "Success" : "Failure");
        return success;
    } else {
        LOG_ERROR << "Worker thread: Timeout waiting for result publish " \
                     "confirmation callback.";
        return false;
    }
}

void Worker::acknowledgeTask(uint64_t deliveryTag, const std::string& task_uuid,
                     int part_number) {
    if (!channel_ || !channel_->usable()) {
        LOG_ERROR << "IO Thread: Cannot ACK task " << task_uuid << ", part "
                  << part_number << " (tag " << deliveryTag
                  <<"): Channel not usable.";
        return;
    }
    try {
        channel_->ack(deliveryTag);
        LOG_INFO << "IO Thread: ACKed task " << task_uuid << ", part "
                 << part_number << " (tag " << deliveryTag << ")";
    } catch (const std::exception& e) {
        LOG_ERROR << "IO Thread: Failed to ACK task " << task_uuid
                  << ", part " << part_number << " (tag "
                  << deliveryTag << "): " << e.what();
    }
}
