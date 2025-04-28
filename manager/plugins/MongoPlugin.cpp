//
// Created by Contarr on 23.04.2025.
//

#include "MongoPlugin.h"
#include <drogon/drogon.h>
#include <mongocxx/uri.hpp>
#include <iostream>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

void MongoPlugin::initAndStart(const Json::Value& config) {
    if (config.isMember("mongo_uri") && config["mongo_uri"].isString()) {
        uri_string_ = config["mongo_uri"].asString();
        try
        {
            mongo_instance_ = std::make_unique<mongocxx::instance>();
            mongocxx::uri uri{uri_string_};
            mongo_pool_ = std::make_unique<mongocxx::pool>(uri);
            LOG_INFO << "MongoDB plugin initialized successfully.";
        } catch (const std::exception& e)
        {
            LOG_ERROR << "Failed to initialize MongoDB pool in plugin: " << e.what();
        }
    } else {
        std::cerr << "Error: 'mongo_uri' not found or is not a string in the " \
                     "plugin configuration." << std::endl;
        exit(EXIT_FAILURE);
    }
}

void MongoPlugin::shutdown() {
    mongo_pool_.reset();
    mongo_instance_.reset();
    LOG_INFO << "MongoDB plugin stopped.";
}

mongocxx::pool::entry MongoPlugin::getMongoConnection() {
    if (mongo_pool_) {
        return mongo_pool_->acquire();
    }
    LOG_ERROR << "Failed to get mongodb pool entry in plugin.";

}