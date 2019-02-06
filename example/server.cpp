#include "../include/remote/rpc_server.h"

#include <boost/asio.hpp>

#include <functional>
#include <iostream>
#include <string>
#include <tuple>

int main() {
    boost::asio::io_service io_service;
    remote::rpc_server s{io_service, "0.0.0.0", 12345};
    std::function<std::tuple<int, std::string> (int)> foo_handler = [](int i) {
        return std::tuple<int, std::string>(i + 1000, "foo");
    };
    std::function<std::tuple<double> (double)> sin_handler = [](double i) {
        return std::sin(i);
    };
    s.add_procedure("foo", foo_handler);
    s.add_procedure("sin", sin_handler);
    io_service.run();
}
