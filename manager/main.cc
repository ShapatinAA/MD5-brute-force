#include <drogon/drogon.h>
#include <plugins/MongoPlugin.h>
#include <plugins/StartupPlugin.h>

using namespace drogon;

int main()
{
    // LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    // LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    LOG_INFO << "Server running on 127.0.0.1:8848";
    app().getLoop()->runAfter(0.0, [](){
        app().getPlugin<StartupPlugin>()->resumeWork();
    });
    app().loadConfigFile("config.json").run();
    return 0;
}
