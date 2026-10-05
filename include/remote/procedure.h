#ifndef REMOTE_PROCEDURE_H
#define REMOTE_PROCEDURE_H

#include <msgpack.hpp>

#include <string_view>
#include <optional>
#include <chrono>

namespace remote {

/// A call of a procedure with its arguments bound and serialized, ready to be sent by a client.
template<typename Result>
class invocation {
public:
    using result_type = Result;

    invocation(std::string_view method, msgpack::sbuffer params) noexcept
            : method_{method}
            , params_{std::move(params)} {
    }

    std::string_view method() const noexcept { return method_; }

    /// The arguments as a serialized msgpack array.
    const msgpack::sbuffer &params() const noexcept { return params_; }

    std::optional<std::chrono::milliseconds> timeout() const noexcept { return timeout_; }

    invocation with_timeout(std::chrono::milliseconds timeout) && {
        timeout_ = timeout;
        return std::move(*this);
    }

private:
    std::string_view method_;
    msgpack::sbuffer params_;
    std::optional<std::chrono::milliseconds> timeout_;
};

template<typename Signature>
class procedure;

/// Names a remote procedure and fixes its signature.
///
/// A procedure is declared once, typically in a header shared by client and server:
///
///     inline constexpr remote::procedure<int(int, int)> add{"add"};
///
/// The server registers a handler for it, and the client calls it with bound arguments:
///
///     server.add_procedure(add, [](int a, int b) { return a + b; });
///     int sum = co_await client.async_call(add(1, 2));
///
/// Both sides are type-checked against the same declaration at compile time. Argument and
/// result types must be serializable with msgpack.
template<typename Result, typename... Args>
class procedure<Result(Args...)> {
public:
    using result_type = Result;

    constexpr explicit procedure(std::string_view name) noexcept
            : name_{name} {
    }

    constexpr std::string_view name() const noexcept { return name_; }

    /// Binds arguments to the procedure. The arguments are serialized right away, so they do
    /// not need to outlive the returned invocation.
    invocation<Result> operator()(const Args &... args) const {
        msgpack::sbuffer params;
        msgpack::packer<msgpack::sbuffer> packer{params};
        packer.pack_array(sizeof...(Args));
        (packer.pack(args), ...);
        return {name_, std::move(params)};
    }

private:
    std::string_view name_;
};

}   // namespace remote

#endif //REMOTE_PROCEDURE_H
