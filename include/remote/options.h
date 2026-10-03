#ifndef REMOTE_OPTIONS_H
#define REMOTE_OPTIONS_H

#include <cstddef>

namespace remote {

/// Settings shared by clients and servers.
struct options {
    /// Connections whose peer sends a larger message are closed with `error::message_too_large`.
    std::size_t max_message_size = 64 * 1024 * 1024;
};

}   // namespace remote

#endif //REMOTE_OPTIONS_H
