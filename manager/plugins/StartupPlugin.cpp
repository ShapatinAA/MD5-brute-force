//
// Created by Contarr on 25.04.2025.
//

#include "StartupPlugin.h"
#include "Alphabet.h"
#include "CrackStatuses.h"
#include "ManagerToWorkerDTO.h"

#include <drogon/drogon.h>
#include <mongocxx/uri.hpp>
#include <iostream>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

using namespace bsoncxx;
using namespace CrackStatuses;
using namespace CrackingAlphabet;

using builder::basic::make_document;
using builder::basic::kvp;
using builder::basic::make_array;


void StartupPlugin::initAndStart(const Json::Value& config) {}
void StartupPlugin::shutdown() {}

void StartupPlugin::resumeWork(mongocxx::collection &collection) {
      getUndistributedJobParts(collection);
}

void StartupPlugin::getUndistributedJobParts(mongocxx::collection &collection) {
    auto number_of_found_docs =
        collection.find(make_document(kvp(
            "workers_done_statuses",
            make_document(kvp("$elemMatch", make_document(
                kvp("$eq", WorkerStatusType[kWaiting])))))
            ));
}


