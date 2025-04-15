 #define NOMINMAX

#include <drogon/drogon.h>

#include <cstdint>
#include <iostream>
#include <vector>

 #include <bsoncxx/builder/basic/document.hpp>
 #include <bsoncxx/json.hpp>
 #include <mongocxx/client.hpp>
 #include <mongocxx/instance.hpp>
 #include <mongocxx/uri.hpp>

using namespace drogon;
 using bsoncxx::builder::basic::kvp;
 using bsoncxx::builder::basic::make_document;

int main()
{

     setlocale(LC_ALL, "en_US.UTF-8");

    try {

        mongocxx::instance instance;
        std::string uri_str = "mongodb://localhost:27017";
        for (char c : uri_str) {
            std::cout << std::hex << static_cast<int>(c) << " ";
        }
        mongocxx::uri uri(uri_str);
        mongocxx::client client(uri);
        auto db = client["sample_mflix"];
        auto collection = db["movies"];
        std::cout << "Connected to MongoDB!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Connection error: " << e.what() << std::endl;
    }

    LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).setThreadNum(0).run();
    // // LOG_INFO << "Server running on 127.0.0.1:8848";
    // app().addListener("127.0.0.1", 8848).setThreadNum(0).run();
    return 0;
}
