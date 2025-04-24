//
// Created by Contarr on 23.04.2025.
//

#pragma once

#include <drogon/drogon.h>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <memory>
#include <string>

class MongoPlugin : public drogon::Plugin<MongoPlugin> {
public:
    void initAndStart(const Json::Value& config) override;
    void shutdown() override;

    mongocxx::pool::entry getMongoConnection();

private:
    std::unique_ptr<mongocxx::instance> mongo_instance_;
    std::unique_ptr<mongocxx::pool> mongo_pool_;
    std::string uri_string_;
};
