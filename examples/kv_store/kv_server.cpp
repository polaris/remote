// An in-memory key-value store served over msgpack-rpc.
//
//     kv_server [port]

#include "kv_api.h"
#include "kv_store.h"

#include <remote/server.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <optional>
#include <algorithm>
#include <atomic>

int main(int argc, char *argv[]) {
    std::atomic<bool> failed{false};

    try {
        const auto port = static_cast<std::uint16_t>(argc > 1 ? std::stoul(argv[1]) : 7070);

        const std::size_t thread_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());

        boost::asio::io_context io{static_cast<int>(thread_count)};
        kv_store store{io.get_executor()};

        remote::server server{io.get_executor(), {boost::asio::ip::tcp::v4(), port}};
        server.add_procedure(kv::put, [&store](std::string key, std::string value) {
            return store.put(std::move(key), std::move(value));
        });
        server.add_procedure(kv::get, [&store](const std::string &key) -> boost::asio::awaitable<std::optional<std::string>> {
            return store.get(key);
        });
        server.add_procedure(kv::erase, [&store](const std::string &key) -> boost::asio::awaitable<bool> {
            return store.erase(key);
        });
        server.add_procedure(kv::keys, [&store]() -> boost::asio::awaitable<std::vector<std::string>> {
            return store.keys();
        });
        server.start();

        boost::asio::signal_set signals{io, SIGINT, SIGTERM};
        signals.async_wait([&](boost::system::error_code, int) {
            server.stop();
        });

        std::cout << "listening on " << server.local_endpoint() << std::endl;

        std::vector<std::jthread> pool;
        try {
            for (unsigned i = 0; i < thread_count; ++i) {
                pool.emplace_back([&io, &failed]() {
                    try {
                        io.run();
                    } catch (const std::exception &ex) {
                        failed.store(true);
                        std::cerr << "error: " << ex.what() << std::endl;
                        io.stop();
                    }
                });
            }
        } catch (...) {
            io.stop();
            throw;
        }
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return 1;
    }

    return failed ? 1 : 0;
}
