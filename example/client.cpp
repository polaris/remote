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

        remote::rpc_client c{io_service, "127.0.0.1", 12345};

        std::mutex mutex;

        remote::rpc_future<int, std::string> foo_result = c.async_call<int, std::string>("foo", 123);
        auto t1 = foo_result.then([&mutex](std::future<std::tuple<int, std::string>>& res) {
            try {
                std::lock_guard<std::mutex> lock{mutex};
                const auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << ", " << std::get<1>(tuple) << std::endl;
            } catch (const std::exception& ex) {
                std::cout << "Failure: " << ex.what() << std::endl;
            }
        });

        remote::rpc_future<double> sin_result = c.async_call<double>("sin", 3.14);
        auto t2 = sin_result.then([&mutex](std::future<std::tuple<double>>& res) {
            try {
                std::lock_guard<std::mutex> lock{mutex};
                const auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << std::endl;
            } catch (const std::exception& ex) {
                std::cout << "Failure: " << ex.what() << std::endl;
            }
        });

        std::vector<int> v{1, 2, 3};

        remote::rpc_future<std::string> complex_result = c.async_call<std::string>("complex", v);
        auto t3 = complex_result.then([&mutex](std::future<std::tuple<std::string>>& res) {
            try {
                std::lock_guard<std::mutex> lock{mutex};
                const auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << std::endl;
            } catch (const std::exception& ex) {
                std::cout << "Failure: " << ex.what() << std::endl;
            }
        });

        uint32_t term = 123;
        uint32_t leader_id = 321;
        uint32_t prev_log_index = 1;
        uint32_t prev_log_term = 2;
        log_entry_vector entries;
        uint32_t leader_commit = 1;

        log_entry le;
        le.command = "command";
        le.index = 123;
        le.term = 321;
        entries.push_back(le);

        remote::rpc_future<uint32_t, bool> append_entries_result = c.async_call<uint32_t, bool>(
            "AppendEntries", term, leader_id, prev_log_index, prev_log_term, entries, leader_commit);
        auto t4 = append_entries_result.then([&mutex](std::future<std::tuple<uint32_t, bool>>& res) {
            try {
                std::lock_guard<std::mutex> lock{mutex};
                const auto tuple = res.get();
                std::cout << "Result: " << std::get<0>(tuple) << ", " << std::get<1>(tuple) << std::endl;
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
