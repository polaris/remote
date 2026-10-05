// A command line client for kv_server.
//
//     kv_client <host> <port> put <key> <value>
//     kv_client <host> <port> get <key>
//     kv_client <host> <port> erase <key>
//     kv_client <host> <port> keys

#include "kv_api.h"

#include <remote/client.h>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>

#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

int usage() {
    std::cerr << "usage: kv_client <host> <port> put <key> <value>\n"
                 "       kv_client <host> <port> get <key>\n"
                 "       kv_client <host> <port> erase <key>\n"
                 "       kv_client <host> <port> keys\n";
    return 2;
}

// Returns the process exit code. The references are safe: client and args live in main(),
// which outlives io.run() and therefore this coroutine.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-reference-coroutine-parameters)
boost::asio::awaitable<int> run(remote::client &client, std::span<const std::string> args) {
    co_await client.async_connect(args[0], args[1]);

    const std::string &command = args[2];
    if (command == "put" && args.size() == 5) {
        co_await client.async_call(kv::put(args[3], args[4]));
    } else if (command == "get" && args.size() == 4) {
        const auto value = co_await client.async_call(kv::get(args[3]));
        if (!value) {
            std::cerr << "not found\n";
            co_return 1;
        }
        std::cout << *value << '\n';
    } else if (command == "erase" && args.size() == 4) {
        if (!co_await client.async_call(kv::erase(args[3]))) {
            std::cerr << "not found\n";
            co_return 1;
        }
    } else if (command == "keys" && args.size() == 3) {
        for (const auto &key : co_await client.async_call(kv::keys())) {
            std::cout << key << '\n';
        }
    } else {
        co_return usage();
    }
    co_return 0;
}

}   // namespace

int main(int argc, char *argv[]) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (args.size() < 3) {
            return usage();
        }

        boost::asio::io_context io;
        remote::client client{io.get_executor()};
        int exit_code = 1;
        boost::asio::co_spawn(io, run(client, args), [&](const std::exception_ptr &ex, int result) {
            exit_code = result;
            if (ex) {
                try {
                    std::rethrow_exception(ex);
                } catch (const boost::system::system_error &error) {
                    std::cerr << "error: " << error.code().message() << '\n';
                    exit_code = 1;
                } catch (const std::exception &error) {
                    std::cerr << "error: " << error.what() << '\n';
                    exit_code = 1;
                }
            }
            client.close();
        });
        io.run();
        return exit_code;
    } catch (const std::exception &ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
