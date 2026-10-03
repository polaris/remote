// An in-memory key-value store served over msgpack-rpc.
//
//     kv_server [port]

#include "kv_api.h"

#include <remote/server.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include <csignal>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>

int main(int argc, char *argv[]) {
    try {
        const auto port = static_cast<std::uint16_t>(argc > 1 ? std::stoul(argv[1]) : 7070);

        boost::asio::io_context io;
        std::map<std::string, std::string> store;

        remote::server server{io.get_executor(), {boost::asio::ip::tcp::v4(), port}};
        server.add_procedure(kv::put, [&store](std::string key, std::string value) {
            store.insert_or_assign(std::move(key), std::move(value));
        });
        server.add_procedure(kv::get, [&store](const std::string &key) -> std::optional<std::string> {
            const auto it = store.find(key);
            if (it == store.end()) {
                return std::nullopt;
            }
            return it->second;
        });
        server.add_procedure(kv::erase, [&store](const std::string &key) {
            return store.erase(key) > 0;
        });
        server.add_procedure(kv::keys, [&store] {
            std::vector<std::string> keys;
            keys.reserve(store.size());
            for (const auto &[key, value] : store) {
                keys.push_back(key);
            }
            return keys;
        });
        server.start();

        boost::asio::signal_set signals{io, SIGINT, SIGTERM};
        signals.async_wait([&](boost::system::error_code, int) {
            server.stop();
        });

        std::cout << "listening on " << server.local_endpoint() << std::endl;
        io.run();
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
