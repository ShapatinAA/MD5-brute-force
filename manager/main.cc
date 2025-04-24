#include <drogon/drogon.h>

using namespace drogon;

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

// #include <amqpcpp.h>
// #include <amqpcpp/libboostasio.h>
//
// #include <boost/asio/io_service.hpp>
// #include <boost/asio/strand.hpp>
// #include <boost/asio/deadline_timer.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

int main()
{

    // access to the boost asio handler
    // note: we suggest use of 2 threads - normally one is fin (we are simply demonstrating thread safety).
    // boost::asio::io_service service(4);
    //
    // // handler for libev
    // AMQP::LibBoostAsioHandler handler(service);
    //
    // // make a connection
    // AMQP::TcpConnection connection(&handler, AMQP::Address("amqp://guest:guest@localhost/"));
    //
    // // we need a channel too
    // AMQP::TcpChannel channel(&connection);
    //
    // // create a temporary queue
    // channel.declareQueue(AMQP::exclusive).onSuccess([&connection](const std::string &name, uint32_t messagecount, uint32_t consumercount) {
    //
    //     // report the name of the temporary queue
    //     std::cout << "declared queue " << name << std::endl;
    //
    //     // now we can close the connection
    //     connection.close();
    // });
    //
    // // run the handler
    // // a t the moment, one will need SIGINT to stop.  In time, should add signal handling through boost API.
    // return service.run();


    // LOG_INFO << "Server running on 0.0.0.0:" << std::getenv("RUNNING_PORT");
    // app().addListener("0.0.0.0", std::stoi(std::getenv("RUNNING_PORT"))).setThreadNum(0).run();
    LOG_INFO << "Server running on 127.0.0.1:8848";
    app().loadConfigFile("config.json").run();
    // app().addListener("127.0.0.1", 8848).setThreadNum(0).run();
    return 0;
}
