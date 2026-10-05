#include <remote/client.h>

#include <remote/detail/connection.h>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/scope/scope_exit.hpp>

#include <chrono>
#include <variant>

namespace remote {

namespace detail {

namespace {

// The point in time a call times out. Zero or negative means never. Timeouts too large to
// add to the current time also mean never; adding them would overflow the time_point.
std::chrono::steady_clock::time_point deadline_after(std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    if (timeout <= std::chrono::milliseconds::zero()) {
        return clock::time_point::max();
    }
    const auto now = clock::now();
    // Compare in milliseconds: converting `timeout` to the clock's nanoseconds could overflow.
    if (timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(clock::time_point::max() - now)) {
        return clock::time_point::max();
    }
    return now + timeout;
}

}   // namespace

// A call waiting for its response. It lives in the frame of the waiting coroutine and is
// registered in pending_calls_ while the coroutine waits.
struct client_impl::pending_call {
    pending_call(const boost::asio::any_io_executor &executor, const connection *owner,
                 std::chrono::steady_clock::time_point deadline)
            : signal{executor, deadline}
            , conn{owner} {
    }

    // The timer expires at the call's deadline. Cancelling it wakes up the waiting coroutine
    // early, when the response has arrived.
    void complete() {
        done = true;
        signal.cancel();
    }

    boost::asio::steady_timer signal;
    const connection *conn;
    bool done = false;
    msgpack::object_handle response;
    msgpack::object result;
    std::exception_ptr error;
};

client_impl::client_impl(const boost::asio::any_io_executor &executor, const options &opts)
        : executor_{executor}
        , strand_{boost::asio::make_strand(executor)}
        , options_{opts} {
}

boost::asio::awaitable<void> client_impl::connect(std::shared_ptr<client_impl> self,
                                                  boost::asio::ip::tcp::endpoint endpoint) {
    boost::asio::ip::tcp::socket socket{self->strand_};
    co_await socket.async_connect(endpoint, boost::asio::deferred);
    attach(self, std::move(socket));
}

boost::asio::awaitable<void> client_impl::connect(std::shared_ptr<client_impl> self, std::string host,
                                                  std::string service) {
    boost::asio::ip::tcp::resolver resolver{self->strand_};
    const auto endpoints = co_await resolver.async_resolve(host, service, boost::asio::deferred);
    boost::asio::ip::tcp::socket socket{self->strand_};
    co_await boost::asio::async_connect(socket, endpoints, boost::asio::deferred);
    attach(self, std::move(socket));
}

void client_impl::attach(const std::shared_ptr<client_impl> &self, boost::asio::ip::tcp::socket socket) {
    if (self->connection_) {
        // The old connection's receive loop fails the calls that are still waiting on it.
        self->connection_->close();
    }
    self->connection_ = std::make_shared<connection>(std::move(socket), self->options_);
    boost::asio::co_spawn(self->strand_, receive_responses(self, self->connection_), boost::asio::detached);
}

boost::asio::awaitable<msgpack::object_handle> client_impl::transact(std::shared_ptr<client_impl> self,
                                                                     std::uint32_t msgid,
                                                                     msgpack::sbuffer request,
                                                                     std::optional<std::chrono::milliseconds> timeout) {
    if (!self->connection_ || !self->connection_->is_open()) {
        throw boost::system::system_error{error::not_connected};
    }

    // A timeout set on the invocation overrides the client's default.
    const auto deadline = deadline_after(timeout.value_or(self->options_.call_timeout));
    pending_call call{self->strand_, self->connection_.get(), deadline};
    self->pending_calls_.emplace(msgid, &call);
    const boost::scope::scope_exit unregister{[&] { self->pending_calls_.erase(msgid); }};

    self->connection_->send(std::move(request));
    if (!call.done) {
        auto [ec] = co_await call.signal.async_wait(boost::asio::as_tuple(boost::asio::deferred));
        if (!call.done) {
            throw boost::system::system_error{ec ? boost::asio::error::operation_aborted
                                             : make_error_code(error::timed_out)};
        }
    }
    if (call.error) {
        std::rethrow_exception(call.error);
    }
    // The result refers to the zone owned by the response handle.
    co_return msgpack::object_handle{call.result, std::move(call.response.zone())};
}

boost::asio::awaitable<void> client_impl::receive_responses(std::shared_ptr<client_impl> self,
                                                            std::shared_ptr<connection> conn) {
    boost::system::error_code reason;
    try {
        for (;;) {
            msgpack::object_handle handle = co_await conn->receive();
            const message msg = parse_message(handle.get());
            const auto *resp = std::get_if<response>(&msg);
            if (resp == nullptr) {
                // Requests and notifications from the server are not supported.
                continue;
            }
            const auto it = self->pending_calls_.find(resp->msgid);
            if (it == self->pending_calls_.end()) {
                // The call was cancelled.
                continue;
            }
            pending_call &call = *it->second;
            if (resp->error.is_nil()) {
                call.result = resp->result;
                call.response = std::move(handle);
            } else {
                call.error = make_exception(resp->error);
            }
            call.complete();
        }
    } catch (const boost::system::system_error &ex) {
        reason = ex.code();
    } catch (...) {
        reason = error::protocol_error;
    }
    conn->close();
    if (self->connection_ == conn) {
        self->connection_.reset();
    }
    self->fail_pending_calls(conn.get(), reason);
}

void client_impl::fail_pending_calls(const connection *conn, const boost::system::error_code &ec) {
    for (const auto &[msgid, call] : pending_calls_) {
        if (call->conn == conn && !call->done) {
            call->error = std::make_exception_ptr(boost::system::system_error{ec});
            call->complete();
        }
    }
}

void client_impl::notify(msgpack::sbuffer notification) {
    boost::asio::dispatch(strand_, [self = shared_from_this(),
                                    notification = std::move(notification)]() mutable {
        if (self->connection_) {
            self->connection_->send(std::move(notification));
        }
    });
}

void client_impl::close() {
    boost::asio::dispatch(strand_, [self = shared_from_this()] {
        if (self->connection_) {
            self->connection_->close();
            self->connection_.reset();
        }
    });
}

std::uint32_t client_impl::next_msgid() noexcept {
    return next_msgid_.fetch_add(1, std::memory_order_relaxed);
}

}   // namespace detail

client::client(const executor_type &executor, const options &opts)
        : impl_{std::make_shared<detail::client_impl>(executor, opts)} {
}

client::~client() {
    try {
        impl_->close();
    } catch (...) {  // NOLINT(bugprone-empty-catch): a destructor must not throw
        // close() fails only if it cannot queue work on the strand, e.g. when out of memory.
        // The connection is then closed when the io_context is destroyed.
    }
}

void client::close() {
    impl_->close();
}

client::executor_type client::get_executor() const noexcept {
    return impl_->executor();
}

}   // namespace remote
