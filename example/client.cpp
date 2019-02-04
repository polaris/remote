#include "../include/remote/detail/then.h"

#include "../include/remote/rpc_client.h"

#include <boost/asio.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>

int main() {
    try {
        std::mutex io_mutex;

        boost::asio::io_service io_service;

        rpc_client c{io_service, "127.0.0.1", 12345};

        std::cout << "on main thread " << std::this_thread::get_id() << std::endl;

        rpc_future<int, std::string> foo_result = c.async_call<int, std::string>("foo", 123);
        auto t1 = foo_result.then([&io_mutex](std::future<std::tuple<int, std::string>>& res) {
            std::lock_guard<std::mutex> guard{io_mutex};
            std::cout << "on t1 thread " << std::this_thread::get_id() << std::endl;
            try {
                const auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << ", " << std::get<1>(tuple) << std::endl;
            } catch (const std::exception& ex) {
                std::cout << "Failure: " << ex.what() << std::endl;
            }
        });

        rpc_future<double> sin_result = c.async_call<double>("sin", 3.14);
        auto t2 = sin_result.then([&io_mutex](std::future<std::tuple<double>>& res) {
            std::lock_guard<std::mutex> guard{io_mutex};
            std::cout << "on t2 thread " << std::this_thread::get_id() << std::endl;
            try {
                auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << std::endl;
            } catch (const std::exception& ex) {
                std::cout << "Failure: " << ex.what() << std::endl;
            }
        });

        io_service.run();

    } catch (const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}
