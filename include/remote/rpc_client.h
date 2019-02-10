#ifndef RPC_RPC_CLIENT_H
#define RPC_RPC_CLIENT_H

#include "detail/then.h"
#include "detail/call.h"

#include <msgpack.hpp>

#include <boost/asio.hpp>

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

namespace remote {

template<typename... ReturnValueTypes>
class rpc_future {
public:
    explicit rpc_future(std::future<std::tuple<ReturnValueTypes...>> future);

    std::tuple<ReturnValueTypes...> get();

    void wait();

    template<typename Rep, typename Period>
    std::future_status wait_for(const std::chrono::duration<Rep, Period> &duration);

    template<typename Clock, typename Duration>
    std::future_status wait_until(const std::chrono::time_point<Clock, Duration> &time_point);

    template<typename Function>
    std::future<typename std::result_of<Function(std::future<std::tuple<ReturnValueTypes...>> &)>::type>
    then(Function &&f);

private:
    std::future<std::tuple<ReturnValueTypes...>> future_;
};

template<typename... ReturnValueTypes>
std::tuple<ReturnValueTypes...> rpc_future<ReturnValueTypes...>::get() {
    return future_.get();
}

template<typename... ReturnValueTypes>
rpc_future<ReturnValueTypes...>::rpc_future(std::future<std::tuple<ReturnValueTypes...>> future)
        : future_{std::move(future)} {
}

template<typename... ReturnValueTypes>
void rpc_future<ReturnValueTypes...>::wait() {
    future_.wait();
}

template<typename... ReturnValueTypes>
template<typename Rep, typename Period>
std::future_status rpc_future<ReturnValueTypes...>::wait_for(const std::chrono::duration<Rep, Period> &duration) {
    return future_.wait_for(duration);
}

template<typename... ReturnValueTypes>
template<typename Clock, typename Duration>
std::future_status
rpc_future<ReturnValueTypes...>::wait_until(const std::chrono::time_point<Clock, Duration> &time_point) {
    return future_.wait_until(time_point);
}

template<typename... ReturnValueTypes>
template<typename Function>
std::future<typename std::result_of<Function(std::future<std::tuple<ReturnValueTypes...>> &)>::type>
rpc_future<ReturnValueTypes...>::then(Function &&f) {
    return detail::then(future_, std::forward<Function>(f));
}

class rpc_client {
public:
    rpc_client(boost::asio::io_service &io_service, const char *address, uint16_t port);

    template<typename... ReturnValueTypes, typename... ArgumentTypes>
    std::tuple<ReturnValueTypes...> call(const std::string &procedure_name, ArgumentTypes... arguments);

    template<typename... ReturnValueTypes, typename... ArgumentTypes>
    rpc_future<ReturnValueTypes...> async_call(const std::string &procedure_name, ArgumentTypes... arguments);

private:
    void connect();

    void read();

    void write(const std::shared_ptr<detail::call_t> &call);

    void send_next_call();

    uint32_t next_call_id() { return next_call_id_++; }

    boost::asio::io_service &io_service_;
    boost::asio::ip::tcp::socket socket_;
    const std::string address_;
    const uint16_t port_;
    uint32_t next_call_id_;
    std::deque<std::shared_ptr<detail::call_t>> queue_;
    std::unordered_map<uint32_t, std::shared_ptr<detail::call_t>> ongoing_calls_;
    msgpack::unpacker unpacker_;
};

template<typename... ReturnsValueTypes, typename... ArgumentTypes>
std::tuple<ReturnsValueTypes...> rpc_client::call(const std::string &procedure_name, ArgumentTypes... arguments) {
    rpc_future<ReturnsValueTypes...> future =
            async_call(procedure_name, std::forward<ArgumentTypes>(arguments)...);
    future.wait();
    return future.get();
}

template<typename... ReturnValueTypes, typename... ArgumentTypes>
rpc_future<ReturnValueTypes...> rpc_client::async_call(const std::string &procedure_name, ArgumentTypes... arguments) {
    if (!socket_.is_open()) {
        connect();
        read();
    }
    const uint32_t call_id = next_call_id();
    auto call = std::make_shared<detail::call_t>();
    call->call_id = call_id;
    msgpack::pack(call->buffer, std::make_tuple(call->call_id, procedure_name, std::make_tuple(arguments...)));
    auto promise = std::make_shared<std::promise<std::tuple<ReturnValueTypes...>>>();
    call->response_handler = [promise](bool success, const msgpack::object &obj) {
        try {
            if (success) {
                promise->set_value(obj.as<std::tuple<ReturnValueTypes...>>());
            } else {
                throw std::runtime_error{obj.as<std::string>()};
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    call->write_handler = [this, call_id, promise](boost::system::error_code ec, std::size_t bytes_sent) {
        try {
            if (ec) {
                throw std::runtime_error{ec.message()};
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    write(call);
    return rpc_future<ReturnValueTypes...>{promise->get_future()};
}

}   // namespace remote

#endif //RPC_RPC_CLIENT_H
