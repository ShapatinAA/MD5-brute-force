//
// Created by Contarr on 25.04.2025.
//

#include "StartupPlugin.h"

#include <drogon/drogon.h>
#include <mongocxx/uri.hpp>
#include <iostream>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

void StartupPlugin::initAndStart(const Json::Value& config) {}
void StartupPlugin::shutdown() {}

void StartupPlugin::resumeWork(mongocxx::collection &collection) {
      getUndistributedJobParts(collection);
}

void StartupPlugin::getUndistributedJobParts(mongocxx::collection &collection) {

}


