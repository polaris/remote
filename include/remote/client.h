#ifndef REMOTE_CLIENT_H
#define REMOTE_CLIENT_H

#include "error.h"
#include "options.h"
#include "procedure.h"
#include "detail/protocol.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

#include <msgpack.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace remote {

namespace detail {

class connection;

class client_impl : public std::enable_shared_from_this<client_impl> {
public:
    client_impl(const boost::asio::any_io_executor &executor, const options &opts);

    static boost::asio::awaitable<void> connect(std::shared_ptr<client_impl> self,
                                                boost::asio::ip::tcp::endpoint endpoint);

    static boost::asio::awaitable<void> connect(std::shared_ptr<client_impl> self, std::string host,
                                                std::string service);

    template<typename Result>
    static boost::asio::awaitable<Result> call(std::shared_ptr<client_impl> self, std::uint32_t msgid,
                                               msgpack::sbuffer request);

    void notify(msgpack::sbuffer notification);

    void close();

    std::uint32_t next_msgid() noexcept;

    const boost::asio::any_io_executor &executor() const noexcept { return executor_; }

    const boost::asio::any_io_executor &strand() const noexcept { return strand_; }

private:
    struct pending_call;

    static boost::asio::awaitable<void> receive_responses(std::shared_ptr<client_impl> self,
                                                          std::shared_ptr<connection> conn);

    static void attach(const std::shared_ptr<client_impl> &self, boost::asio::ip::tcp::socket socket);

    /// Sends a request and waits for the response. Returns the handle that owns the result.
    static boost::asio::awaitable<msgpack::object_handle> transact(std::shared_ptr<client_impl> self,
                                                                   std::uint32_t msgid,
                                                                   msgpack::sbuffer request);

    void fail_pending_calls(const connection *conn, const boost::system::error_code &ec);

    const boost::asio::any_io_executor executor_;
    const boost::asio::any_io_executor strand_;
    const options options_;
    std::atomic<std::uint32_t> next_msgid_{0};

    // Accessed on strand_ only.
    std::shared_ptr<connection> connection_;
    std::unordered_map<std::uint32_t, pending_call *> pending_calls_;
};

template<typename Result>
boost::asio::awaitable<Result> client_impl::call(std::shared_ptr<client_impl> self, std::uint32_t msgid,
                                                 msgpack::sbuffer request) {
    const msgpack::object_handle result = co_await transact(std::move(self), msgid, std::move(request));
    if constexpr (!std::is_void_v<Result>) {
        try {
            co_return result.get().as<Result>();
        } catch (const msgpack::type_error &) {
            throw boost::system::system_error{error::invalid_result};
        }
    }
}

}   // namespace detail

/// Connects to a msgpack-rpc server and calls procedures on it.
///
/// All member functions are thread-safe. Calls may be issued concurrently; they are pipelined
/// over one connection and their responses may arrive in any order.
///
/// Asynchronous operations complete with an std::exception_ptr instead of an error code, so
/// that errors reported by the server keep their message. When used with
/// boost::asio::use_awaitable, boost::asio::deferred or boost::asio::use_future, failures are
/// thrown as boost::system::system_error.
class client {
public:
    using executor_type = boost::asio::any_io_executor;

    explicit client(const executor_type &executor, const options &opts = {});

    client(const client &) = delete;
    client &operator=(const client &) = delete;

    /// Closes the connection. Pending calls fail with boost::asio::error::operation_aborted.
    ~client();

    /// Connects to a server, replacing an existing connection.
    ///
    /// Completion signature: void(std::exception_ptr).
    template<typename Token = boost::asio::default_completion_token_t<executor_type>>
    auto async_connect(const boost::asio::ip::tcp::endpoint &endpoint, Token &&token = {});

    /// Resolves host and service (a port number or service name), then connects to the first
    /// endpoint that accepts the connection.
    ///
    /// Completion signature: void(std::exception_ptr).
    template<typename Token = boost::asio::default_completion_token_t<executor_type>>
    auto async_connect(std::string host, std::string service, Token &&token = {});

    /// Calls a remote procedure:
    ///
    ///     int sum = co_await client.async_call(add(1, 2));
    ///
    /// Completion signature: void(std::exception_ptr, Result), or void(std::exception_ptr) if
    /// Result is void. Supports per-operation cancellation; a cancelled call completes with
    /// boost::asio::error::operation_aborted and its response is discarded.
    template<typename Result, typename Token = boost::asio::default_completion_token_t<executor_type>>
    auto async_call(const invocation<Result> &call, Token &&token = {});

    /// Calls a remote procedure and blocks until it completes.
    ///
    /// The executor must be run by another thread. Calling this from a thread that runs the
    /// executor deadlocks.
    template<typename Result>
    Result call(const invocation<Result> &call);

    /// Sends a notification: a call that has no response. Errors are not reported.
    template<typename Result>
    void notify(const invocation<Result> &call);

    /// Closes the connection. Pending calls fail with boost::asio::error::operation_aborted.
    void close();

    executor_type get_executor() const noexcept;

private:
    std::shared_ptr<detail::client_impl> impl_;
};

template<typename Token>
auto client::async_connect(const boost::asio::ip::tcp::endpoint &endpoint, Token &&token) {
    return boost::asio::co_spawn(impl_->strand(), detail::client_impl::connect(impl_, endpoint),
                                 std::forward<Token>(token));
}

template<typename Token>
auto client::async_connect(std::string host, std::string service, Token &&token) {
    return boost::asio::co_spawn(impl_->strand(),
                                 detail::client_impl::connect(impl_, std::move(host), std::move(service)),
                                 std::forward<Token>(token));
}

template<typename Result, typename Token>
auto client::async_call(const invocation<Result> &call, Token &&token) {
    const std::uint32_t msgid = impl_->next_msgid();
    return boost::asio::co_spawn(
            impl_->strand(),
            detail::client_impl::call<Result>(impl_, msgid, detail::pack_request(msgid, call.method(), call.params())),
            std::forward<Token>(token));
}

template<typename Result>
Result client::call(const invocation<Result> &call) {
    return async_call(call, boost::asio::use_future).get();
}

template<typename Result>
void client::notify(const invocation<Result> &call) {
    impl_->notify(detail::pack_notification(call.method(), call.params()));
}

}   // namespace remote

#endif //REMOTE_CLIENT_H
