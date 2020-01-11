#include "log_entries.h"
#include "../include/remote/rpc_server.h"

#include <boost/asio.hpp>

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

int main() {
    boost::asio::io_service io_service;

    remote::rpc_server s{io_service, "0.0.0.0", 12345};

    std::function<std::tuple<int, std::string> (int)> foo_handler = [](int i) {
        return std::make_tuple<int, std::string>(i + 1000, "foo");
    };
    s.add_procedure("foo", foo_handler);

    std::function<std::tuple<double> (double)> sin_handler = [](double i) {
        return std::make_tuple<double>(std::sin(i));
    };
    s.add_procedure("sin", sin_handler);

    std::function<std::tuple<std::string> (std::vector<int>)> complex_handler = [](std::vector<int> v) {
        std::stringstream ss;
        for (int i : v) {
            ss << i << " ";
        }
        return std::make_tuple<std::string>(ss.str());
    };
    s.add_procedure("complex", complex_handler);

    std::function<std::tuple<uint32_t, bool> (uint32_t, uint32_t, uint32_t, uint32_t, log_entry_vector, uint32_t)> append_entries_handler = [](
        uint32_t term,
        uint32_t leader_id,
        uint32_t prev_log_index,
        uint32_t prev_log_term,
        log_entry_vector entries,
        uint32_t leader_commit) {

        return std::make_tuple<uint32_t, bool>(entries.size(), false);
    };
    s.add_procedure("AppendEntries", append_entries_handler);

    io_service.run();
}
