#ifndef REMOTE_OPTIONS_H
#define REMOTE_OPTIONS_H

#include <chrono>
#include <cstddef>

namespace remote {

/// Settings shared by clients and servers.
struct options {
    /// Connections whose peer sends a larger message are closed with `error::message_too_large`.
    std::size_t max_message_size = 64 * 1024 * 1024;

    /// Client only: calls without a response within this time fail with `error::timed_out`.
    /// Zero or negative means no timeout. A timeout is local: the server may still execute
    /// the call.
    std::chrono::milliseconds call_timeout{0};
};

}   // namespace remote

#endif //REMOTE_OPTIONS_H
