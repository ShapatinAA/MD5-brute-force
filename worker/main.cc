#include <controllers/Worker.h>
#include <drogon/drogon.h>

using namespace drogon;

int main() {
    try {
        Worker worker;
        LOG_INFO << "Worker created. Starting now.";
        worker.run();
    } catch (const std::exception& e) {
        LOG_FATAL << "Unhandled exception in main: " << e.what();
    }
    return 0;
}

