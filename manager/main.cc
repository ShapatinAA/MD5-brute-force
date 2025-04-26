#include <drogon/drogon.h>
#include <plugins/MongoPlugin.h>
#include <plugins/StartupPlugin.h>

using namespace drogon;

int main()
{
    // LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    // app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).setThreadNum(0).run();
    LOG_INFO << "Server running on 127.0.0.1:8848";
    // app().getLoop()->runAt(0, initStartRoutine());
    // app().getLoop()->runEvery(40.0, [](){
    //     auto client = app().getPlugin<MongoPlugin>()->getMongoConnection();
    //     auto collection = client["MD5HashCrack"]["Results"];
    //     app().getPlugin<StartupPlugin>()->resumeWork(collection);
    // });
    app().loadConfigFile("config.json").run();
    return 0;
}
