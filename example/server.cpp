#include "../include/remote/rpc_server.h"

#include <boost/asio.hpp>

#include <functional>
#include <string>
#include <tuple>

int main() {
    boost::asio::io_service io_service;
    rpc_server s{io_service, "0.0.0.0", 12345};
    std::function<std::tuple<int, std::string> (int)> handler = [](int i) -> std::tuple<int, std::string> {
        return std::tuple<int, std::string>(i + 1000, "foo");
    };
    s.add_procedure("foo", handler);
    io_service.run();
}
