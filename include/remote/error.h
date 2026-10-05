#ifndef REMOTE_ERROR_H
#define REMOTE_ERROR_H

#include <boost/system/error_code.hpp>

#include <type_traits>

namespace remote {

/// Errors reported by remote.
///
/// The numeric values of `unknown_procedure`, `invalid_arguments` and `procedure_failed` are
/// part of the wire format: a server sends them in the error object of a response.
enum class error {
    not_connected = 1,      ///< The client has no open connection.
    protocol_error = 2,     ///< The peer sent a message that is not valid msgpack-rpc.
    message_too_large = 3,  ///< An incoming message exceeds the configured size limit.
    unknown_procedure = 4,  ///< The server has no procedure with the requested name.
    invalid_arguments = 5,  ///< The arguments do not match the procedure's signature.
    procedure_failed = 6,   ///< The procedure threw an exception.
    invalid_result = 7,     ///< The result cannot be converted to the declared result type.
    timed_out = 8,          ///< The operation did not complete within the configured timeout.
};

const boost::system::error_category &error_category() noexcept;

inline boost::system::error_code make_error_code(error e) noexcept {
    return {static_cast<int>(e), error_category()};
}

}   // namespace remote

template<>
struct boost::system::is_error_code_enum<remote::error> : std::true_type {};

#endif //REMOTE_ERROR_H
