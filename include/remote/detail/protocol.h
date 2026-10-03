#ifndef REMOTE_DETAIL_PROTOCOL_H
#define REMOTE_DETAIL_PROTOCOL_H

#include "../error.h"

#include <msgpack.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <variant>

// Messages follow the msgpack-rpc specification
// (https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md):
//
//   request       [0, msgid, method, params]
//   response      [1, msgid, error, result]
//   notification  [2, method, params]
//
// A successful response carries nil as error. A failed response produced by remote carries
// [code, message] as error, where code is a value of remote::error.

namespace remote::detail {

enum class message_type : int {
    request = 0,
    response = 1,
    notification = 2,
};

// The msgpack::object members refer to the zone of the object_handle the message was parsed
// from. They are valid only as long as that handle is alive.

struct request {
    std::uint32_t msgid;
    std::string method;
    msgpack::object params;
};

struct response {
    std::uint32_t msgid;
    msgpack::object error;
    msgpack::object result;
};

struct notification {
    std::string method;
    msgpack::object params;
};

using message = std::variant<request, response, notification>;

/// Throws boost::system::system_error with error::protocol_error if obj is not a valid message.
message parse_message(const msgpack::object &obj);

// Messages are assembled from parts that have already been serialized: msgpack values are
// self-delimiting, so a serialized value can be appended to a buffer as is.

msgpack::sbuffer pack_request(std::uint32_t msgid, std::string_view method, const msgpack::sbuffer &params);

msgpack::sbuffer pack_notification(std::string_view method, const msgpack::sbuffer &params);

msgpack::sbuffer pack_result(std::uint32_t msgid, const msgpack::sbuffer &result);

msgpack::sbuffer pack_error(std::uint32_t msgid, error code, std::string_view text);

/// Turns the error object of a response into the exception reported to the caller.
std::exception_ptr make_exception(const msgpack::object &error);

}   // namespace remote::detail

#endif //REMOTE_DETAIL_PROTOCOL_H
