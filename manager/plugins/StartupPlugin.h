//
// Created by Contarr on 25.04.2025.
//

#pragma once

#include <drogon/drogon.h>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <memory>
#include <string>


class StartupPlugin : public drogon::Plugin<StartupPlugin> {
  public:
    void initAndStart(const Json::Value& config) override;
    void shutdown() override;

    void resumeWork(mongocxx::collection &collection);
    void getUndistributedJobParts(mongocxx::collection &collection);
};
