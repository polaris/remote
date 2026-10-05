#include <remote/detail/connection.h>

#include <remote/error.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/system_error.hpp>

namespace remote::detail {

namespace {

constexpr std::size_t read_size = std::size_t{64} * 1024;

}   // namespace

connection::connection(boost::asio::ip::tcp::socket socket, std::size_t max_message_size)
        : socket_{std::move(socket)}
        , max_message_size_{max_message_size} {
    // Requests and responses are small and latency-bound; do not let Nagle's algorithm hold
    // them back. Failure is not fatal: it happens when the peer has already disconnected,
    // which the first read reports.
    boost::system::error_code ignored;
    socket_.set_option(boost::asio::ip::tcp::no_delay{true}, ignored);
}

boost::asio::awaitable<msgpack::object_handle> connection::receive() {
    msgpack::object_handle message;
    for (;;) {
        // The unpacker parses incrementally; count the bytes of the current message as they
        // are consumed, whether or not the message is complete yet.
        const std::size_t unparsed_before = unpacker_.nonparsed_size();
        bool complete = false;
        try {
            complete = unpacker_.next(message);
        } catch (const msgpack::unpack_error &ex) {
            throw boost::system::system_error{error::protocol_error, ex.what()};
        }
        message_size_ += unparsed_before - unpacker_.nonparsed_size();
        // While waiting for the payload of a str, bin or ext value, the unpacker consumes only
        // its header and leaves the payload unparsed, so count the buffered bytes as well.
        const std::size_t pending = complete ? 0 : unpacker_.nonparsed_size();
        if (message_size_ + pending > max_message_size_) {
            throw boost::system::system_error{error::message_too_large};
        }
        if (complete) {
            message_size_ = 0;
            co_return message;
        }
        unpacker_.reserve_buffer(read_size);
        const std::size_t bytes_read = co_await socket_.async_read_some(
                boost::asio::buffer(unpacker_.buffer(), unpacker_.buffer_capacity()), boost::asio::deferred);
        unpacker_.buffer_consumed(bytes_read);
    }
}

void connection::send(msgpack::sbuffer message) {
    if (!socket_.is_open()) {
        return;
    }
    write_queue_.push_back(std::move(message));
    if (write_queue_.size() == 1) {
        write_next();
    }
}

void connection::write_next() {
    // The message stays at the front of the queue until the write completes. Its presence
    // tells send() that a write is in flight, and it keeps the buffer alive.
    const msgpack::sbuffer &message = write_queue_.front();
    boost::asio::async_write(socket_, boost::asio::buffer(message.data(), message.size()),
                             [self = shared_from_this()](boost::system::error_code ec, std::size_t) {
                                 if (ec) {
                                     self->write_queue_.clear();
                                     self->close();
                                     return;
                                 }
                                 self->write_queue_.pop_front();
                                 if (!self->write_queue_.empty()) {
                                     self->write_next();
                                 }
                             });
}

void connection::close() {
    boost::system::error_code ignored;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}

}   // namespace remote::detail
