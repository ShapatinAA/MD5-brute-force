#include <drogon/drogon.h>

using namespace drogon;

int main() {
    LOG_INFO << "Server running on 0.0.0.0:80";
    app().addListener("0.0.0.0", 80).setThreadNum(0).run();
    // LOG_INFO << "Server running on 127.0.0.1:8793";
    // app().addListener("127.0.0.1", 8793).setThreadNum(0).run();
    return 0;
}

