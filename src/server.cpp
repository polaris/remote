#include <remote/server.h>

#include <remote/error.h>
#include <remote/detail/connection.h>
#include <remote/detail/protocol.h>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/deferred.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <variant>

namespace remote {

namespace detail {

class server_impl : public std::enable_shared_from_this<server_impl> {
public:
    server_impl(const boost::asio::any_io_executor &executor, const boost::asio::ip::tcp::endpoint &endpoint,
                const options &opts)
            : executor_{executor}
            , options_{opts}
            , acceptor_{boost::asio::make_strand(executor)} {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address{true});
        acceptor_.bind(endpoint);
        acceptor_.listen();
        local_endpoint_ = acceptor_.local_endpoint();
    }

    void add_handler(std::string_view name, procedure_handler &&handler) {
        if (started_) {
            throw std::logic_error{"procedures must be added before the server is started"};
        }
        const auto [it, inserted] = handlers_.try_emplace(std::string{name}, std::move(handler));
        if (!inserted) {
            throw std::logic_error{"procedure '" + std::string{name} + "' is already registered"};
        }
    }

    void start() {
        if (std::exchange(started_, true)) {
            throw std::logic_error{"the server has already been started"};
        }
        boost::asio::co_spawn(acceptor_.get_executor(), accept(shared_from_this()), boost::asio::detached);
    }

    void stop() {
        boost::asio::dispatch(acceptor_.get_executor(), [self = shared_from_this()] {
            boost::system::error_code ignored;
            self->acceptor_.close(ignored);
        });
        const std::scoped_lock lock{mutex_};
        stopped_ = true;
        for (const auto &[ptr, weak_conn] : connections_) {
            if (auto conn = weak_conn.lock()) {
                boost::asio::dispatch(conn->executor(), [conn] { conn->close(); });
            }
        }
    }

    const boost::asio::any_io_executor &executor() const noexcept { return executor_; }

    const boost::asio::ip::tcp::endpoint &local_endpoint() const noexcept { return local_endpoint_; }

private:
    static boost::asio::awaitable<void> accept(std::shared_ptr<server_impl> self) {
        while (self->acceptor_.is_open()) {
            auto [ec, socket] = co_await self->acceptor_.async_accept(
                    boost::asio::make_strand(self->executor_), boost::asio::as_tuple(boost::asio::deferred));
            if (ec == boost::asio::error::operation_aborted) {
                break;
            }
            if (ec) {
                // Typically the process ran out of file descriptors. Back off instead of
                // spinning until connections are closed.
                boost::asio::steady_timer backoff{self->acceptor_.get_executor(), std::chrono::milliseconds{100}};
                co_await backoff.async_wait(boost::asio::as_tuple(boost::asio::deferred));
                continue;
            }
            auto conn = std::make_shared<connection>(std::move(socket), self->options_);
            {
                const std::scoped_lock lock{self->mutex_};
                if (self->stopped_) {
                    conn->close();
                    break;
                }
                self->connections_.emplace(conn.get(), conn);
            }
            boost::asio::co_spawn(conn->executor(), serve(self, conn), boost::asio::detached);
        }
    }

    static boost::asio::awaitable<void> serve(std::shared_ptr<server_impl> self, std::shared_ptr<connection> conn) {
        try {
            for (;;) {
                msgpack::object_handle handle = co_await conn->receive();
                message msg = parse_message(handle.get());
                if (auto *req = std::get_if<request>(&msg)) {
                    // Each request runs in its own coroutine, so a handler that waits does not
                    // hold up the requests behind it.
                    boost::asio::co_spawn(conn->executor(),
                                          handle_request(self, conn, std::move(*req), std::move(handle)),
                                          boost::asio::detached);
                } else if (auto *notif = std::get_if<notification>(&msg)) {
                    boost::asio::co_spawn(conn->executor(),
                                          handle_notification(self, std::move(*notif), std::move(handle)),
                                          boost::asio::detached);
                }
                // Responses are ignored: the server never sends requests.
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): ending the loop is the handling
            // The peer disconnected or violated the protocol.
        }
        conn->close();
        const std::scoped_lock lock{self->mutex_};
        self->connections_.erase(conn.get());
    }

    // `handle` owns the memory that `req.params` refers to; it is passed in to keep it alive.
    static boost::asio::awaitable<void> handle_request(std::shared_ptr<server_impl> self,
                                                       std::shared_ptr<connection> conn, request req,
                                                       [[maybe_unused]] msgpack::object_handle handle) {
        msgpack::sbuffer response;
        try {
            const auto it = self->handlers_.find(req.method);
            if (it == self->handlers_.end()) {
                response = pack_error(req.msgid, error::unknown_procedure, "unknown procedure '" + req.method + "'");
            } else {
                boost::asio::awaitable<msgpack::sbuffer> invocation = it->second(req.params);
                response = pack_result(req.msgid, co_await std::move(invocation));
            }
        } catch (const invalid_arguments &ex) {
            response = pack_error(req.msgid, error::invalid_arguments, ex.what());
        } catch (const std::exception &ex) {
            response = pack_error(req.msgid, error::procedure_failed, ex.what());
        } catch (...) {
            response = pack_error(req.msgid, error::procedure_failed, "unknown exception");
        }
        conn->send(std::move(response));
    }

    static boost::asio::awaitable<void> handle_notification(std::shared_ptr<server_impl> self, notification notif,
                                                            [[maybe_unused]] msgpack::object_handle handle) {
        try {
            const auto it = self->handlers_.find(notif.method);
            if (it != self->handlers_.end()) {
                co_await it->second(notif.params);
            }
        } catch (...) {  // NOLINT(bugprone-empty-catch): see below
            // Notifications have no response, so there is nobody to report the error to.
        }
    }

    const boost::asio::any_io_executor executor_;
    const options options_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ip::tcp::endpoint local_endpoint_;
    bool started_ = false;

    // Written before start() only, read concurrently afterwards.
    std::unordered_map<std::string, procedure_handler> handlers_;

    std::mutex mutex_;
    bool stopped_ = false;
    std::unordered_map<const connection *, std::weak_ptr<connection>> connections_;
};

}   // namespace detail

server::server(const executor_type &executor, const boost::asio::ip::tcp::endpoint &endpoint, const options &opts)
        : impl_{std::make_shared<detail::server_impl>(executor, endpoint, opts)} {
}

server::~server() {
    try {
        stop();
    } catch (...) {  // NOLINT(bugprone-empty-catch): a destructor must not throw
        // stop() fails only if it cannot queue work, e.g. when out of memory. The connections
        // are then closed when the io_context is destroyed.
    }
}

void server::start() {
    impl_->start();
}

void server::stop() {
    impl_->stop();
}

boost::asio::ip::tcp::endpoint server::local_endpoint() const {
    return impl_->local_endpoint();
}

server::executor_type server::get_executor() const {
    return impl_->executor();
}

void server::add_handler(std::string_view name, detail::procedure_handler &&handler) {
    impl_->add_handler(name, std::move(handler));
}

}   // namespace remote
