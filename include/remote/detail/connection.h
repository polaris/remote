#ifndef REMOTE_DETAIL_CONNECTION_H
#define REMOTE_DETAIL_CONNECTION_H

#include "../options.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <msgpack.hpp>

#include <cstddef>
#include <deque>
#include <memory>

namespace remote::detail {

/// A TCP stream of msgpack messages.
///
/// Not thread-safe: all member functions must be called on the socket's executor, which is
/// expected to be a strand. Outgoing messages are queued so that at most one write is in
/// flight at any time.
class connection : public std::enable_shared_from_this<connection> {
public:
    connection(boost::asio::ip::tcp::socket socket, const options &opts);

    /// Waits for the next complete message. Throws boost::system::system_error when the
    /// connection fails or the peer violates the protocol.
    boost::asio::awaitable<msgpack::object_handle> receive();

    /// Queues a message for sending. Messages sent on a closed connection are dropped.
    void send(msgpack::sbuffer message);

    void close();

    bool is_open() const { return socket_.is_open(); }

    boost::asio::any_io_executor executor() { return socket_.get_executor(); }

private:
    void write_next();

    boost::asio::ip::tcp::socket socket_;
    const std::size_t max_message_size_;
    std::size_t message_size_ = 0;
    msgpack::unpacker unpacker_;
    std::deque<msgpack::sbuffer> write_queue_;
};

}   // namespace remote::detail

#endif //REMOTE_DETAIL_CONNECTION_H
