#include <drogon/drogon.h>

using namespace drogon;


int main()
{
    LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).run();
    return 0;
}
