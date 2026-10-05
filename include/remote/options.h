#ifndef REMOTE_OPTIONS_H
#define REMOTE_OPTIONS_H

#include <chrono>
#include <cstddef>

namespace remote {

/// Settings shared by clients and servers.
///
/// A connection whose peer sends a message beyond `max_message_size` or `max_elements` is
/// closed. On a client, the calls pending on it fail with `error::message_too_large`. A server
/// cannot report the error: it closes the connection, and the client's calls fail with a
/// transport error such as `boost::asio::error::eof`.
struct options {
    /// The size of a message in bytes.
    std::size_t max_message_size = std::size_t{64} * 1024 * 1024;

    /// The number of elements in an array, or entries in a map. msgpack allocates storage for
    /// all elements when it reads the header, before they arrive, so this bounds that
    /// allocation. Values below 4, the size of a msgpack-rpc message, are treated as 4.
    std::size_t max_elements = std::size_t{1024} * 1024;

    /// Client only: calls without a response within this time fail with `error::timed_out`.
    /// Zero or negative means no timeout. A timeout is local: the server may still execute
    /// the call.
    std::chrono::milliseconds call_timeout{0};
};

}   // namespace remote

#endif //REMOTE_OPTIONS_H
