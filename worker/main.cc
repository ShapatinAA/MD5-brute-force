#include <drogon/drogon.h>

using namespace drogon;

int main() {
    LOG_INFO << "Server running on 0.0.0.0:80";
    app().addListener("0.0.0.0", 80).run();
    return 0;
}

