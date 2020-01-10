#include "log_entries.h"
#include "../include/remote/rpc_client.h"

#include <boost/asio.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>

int main() {
    try {
        boost::asio::io_service io_service;
        boost::asio::io_service::work work{io_service};

        remote::rpc_client c{io_service, "127.0.0.1", 12345};

        std::thread t{[&io_service](){io_service.run();}};

        std::tuple<int, std::string> foo_result = c.call<int, std::string>("foo", 123);

        std::cout << "Result: " << std::get<0>(foo_result) << ", " << std::get<1>(foo_result) << std::endl;

        io_service.stop();

        t.join();

    } catch (const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}
