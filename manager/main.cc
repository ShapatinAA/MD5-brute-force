#include <drogon/drogon.h>

using namespace drogon;

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

int main()
{

    mongocxx::instance instance;
    mongocxx::uri uri("mongodb://mongo1:27017,mongo2:27017,mongo3:27017/?replicaSet=myReplicaSet");
    mongocxx::client client(uri);
    // LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    // app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).setThreadNum(0).run();
    LOG_INFO << "Server running on 127.0.0.1:8848";
    app().addListener("127.0.0.1", 8848).setThreadNum(0).run();
    return 0;
}
