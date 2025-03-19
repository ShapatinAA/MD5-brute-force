#include <drogon/drogon.h>

using namespace drogon;


int main()
{
    LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).setThreadNum(0).run();
    // LOG_INFO << "Server running on 127.0.0.1:8848";
    // app().addListener("127.0.0.1", 8848).setThreadNum(0).run();
    return 0;
}
