#ifndef REMOTE_SERVER_H
#define REMOTE_SERVER_H

#include "options.h"
#include "procedure.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <msgpack.hpp>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace remote {

namespace detail {

/// Converts the params array of a request into the procedure's arguments, then returns an
/// awaitable that runs the handler and yields its serialized result.
using procedure_handler =
        std::function<boost::asio::awaitable<msgpack::sbuffer>(const msgpack::object &params)>;

struct invalid_arguments : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class server_impl;

template<typename T>
struct is_awaitable : std::false_type {};

template<typename T, typename Executor>
struct is_awaitable<boost::asio::awaitable<T, Executor>> : std::true_type {};

template<typename T>
struct awaitable_value {
    using type = T;
};

template<typename T, typename Executor>
struct awaitable_value<boost::asio::awaitable<T, Executor>> {
    using type = T;
};

template<typename Tuple>
Tuple convert_arguments(const msgpack::object &params) {
    constexpr auto arity = std::tuple_size_v<Tuple>;
    if (params.type != msgpack::type::ARRAY || params.via.array.size != arity) {
        throw invalid_arguments{"expected " + std::to_string(arity) + " argument(s)"};
    }
    try {
        return params.as<Tuple>();
    } catch (const msgpack::type_error &) {
        throw invalid_arguments{"argument type mismatch"};
    }
}

template<typename Result, typename Handler, typename Tuple>
boost::asio::awaitable<msgpack::sbuffer> invoke(std::shared_ptr<Handler> handler, Tuple arguments) {
    using handler_result = decltype(std::apply(*handler, std::move(arguments)));
    msgpack::sbuffer packed;
    if constexpr (std::is_void_v<Result>) {
        if constexpr (is_awaitable<handler_result>::value) {
            co_await std::apply(*handler, std::move(arguments));
        } else {
            std::apply(*handler, std::move(arguments));
        }
        msgpack::pack(packed, msgpack::type::nil_t{});
    } else {
        if constexpr (is_awaitable<handler_result>::value) {
            const Result result = co_await std::apply(*handler, std::move(arguments));
            msgpack::pack(packed, result);
        } else {
            const Result result = std::apply(*handler, std::move(arguments));
            msgpack::pack(packed, result);
        }
    }
    co_return packed;
}

}   // namespace detail

/// Accepts TCP connections and serves msgpack-rpc requests.
///
/// Register all procedures, then call start(). Each connection is served on its own strand, so
/// handlers for one connection never run concurrently. Handlers for different connections may
/// run concurrently if the io_context is run by more than one thread.
class server {
public:
    using executor_type = boost::asio::any_io_executor;

    /// Binds to the endpoint immediately. Port 0 selects a free port; see local_endpoint().
    server(const executor_type &executor, const boost::asio::ip::tcp::endpoint &endpoint,
           const options &opts = {});

    server(const server &) = delete;
    server &operator=(const server &) = delete;
    server(server &&) = delete;
    server &operator=(server &&) = delete;

    /// Calls stop().
    ~server();

    /// Registers a handler for a procedure.
    ///
    /// The handler is called with the procedure's arguments. It returns the result, or an
    /// boost::asio::awaitable of the result for handlers that need to wait for I/O. Exceptions
    /// thrown by the handler are reported to the caller as error::procedure_failed.
    ///
    /// Must be called before start(). Throws std::logic_error if the server has been started
    /// or a procedure with the same name is already registered.
    template<typename Result, typename... Args, typename Handler>
    void add_procedure(const procedure<Result(Args...)> &proc, Handler handler);

    /// Starts accepting connections. Throws std::logic_error if the server has been started.
    void start();

    /// Stops accepting connections and closes all open connections. Thread-safe.
    void stop();

    boost::asio::ip::tcp::endpoint local_endpoint() const;

    executor_type get_executor() const;

private:
    void add_handler(std::string_view name, detail::procedure_handler &&handler);

    std::shared_ptr<detail::server_impl> impl_;
};

template<typename Result, typename... Args, typename Handler>
void server::add_procedure(const procedure<Result(Args...)> &proc, Handler handler) {
    using arguments = std::tuple<std::decay_t<Args>...>;
    static_assert(std::is_invocable_v<Handler &, std::decay_t<Args>...>,
                  "handler cannot be called with the procedure's arguments");
    using handler_result = std::invoke_result_t<Handler &, std::decay_t<Args>...>;
    using value = detail::awaitable_value<handler_result>::type;
    static_assert(std::is_void_v<Result> || std::is_convertible_v<value, Result>,
                  "handler result is not convertible to the procedure's result type");

    auto shared_handler = std::make_shared<Handler>(std::move(handler));
    add_handler(proc.name(), [shared_handler](const msgpack::object &params) {
        // Arguments are converted eagerly so that conversion errors are reported as
        // invalid_arguments before the handler runs.
        return detail::invoke<Result>(shared_handler, detail::convert_arguments<arguments>(params));
    });
}

}   // namespace remote

#endif //REMOTE_SERVER_H
