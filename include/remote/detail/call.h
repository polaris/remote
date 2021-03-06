#ifndef REMOTE_CALL_H
#define REMOTE_CALL_H

#include <msgpack.hpp>

#include <boost/asio.hpp>

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

namespace remote {

namespace detail {

struct call_t {
    uint32_t call_id;
    std::function<void(boost::system::error_code, std::size_t)> write_handler;
    std::function<void(bool, const msgpack::object &)> response_handler;
    std::function<void(const std::string&)> error_handler;
    msgpack::sbuffer buffer;
};

}   // namespace detail

}   // namespace remote

#endif //REMOTE_CALL_H
