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
        rpc_client c{io_service, "127.0.0.1", 12345};

        std::future<std::tuple<int, std::string>> foo_result = c.async_call<int, std::string>("foo", 123);
        std::future<std::tuple<double>> sin_result = c.async_call<double>("sin", 3.14);

        std::thread t([&io_service]() {
            io_service.run();
        });

        try {
            auto f = foo_result.get();
            std::cout << "Result: " << std::get<0>(f) << ", " << std::get<1>(f) << std::endl;
        } catch (const std::exception& ex) {
            std::cout << "Failure: " << ex.what() << std::endl;
        }

        try {
            auto f = sin_result.get();
            std::cout << "Result: " << std::get<0>(f) << std::endl;
        } catch (const std::exception& ex) {
            std::cout << "Failure: " << ex.what() << std::endl;
        }

        t.join();

    } catch (const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}
