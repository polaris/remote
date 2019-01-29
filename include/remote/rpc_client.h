#ifndef RPC_RPC_CLIENT_H
#define RPC_RPC_CLIENT_H

#include <msgpack.hpp>

#include <boost/asio.hpp>

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

class rpc_client {
public:
    rpc_client(boost::asio::io_service& io_service, const char* address, uint16_t port);

    template <typename... ReturnValueTypes, typename... ArgumentTypes>
    std::tuple<ReturnValueTypes...> call(const std::string &procedure_name, ArgumentTypes... arguments);

    template <typename... ReturnValueTypes, typename... ArgumentTypes>
    std::future<std::tuple<ReturnValueTypes...>> async_call(const std::string &procedure_name, ArgumentTypes... arguments);

private:
    void connect();
    void read();
    uint32_t next_call_id() { return next_call_id_++; }

    boost::asio::io_service& io_service_;
    boost::asio::ip::tcp::socket socket_;
    const std::string address_;
    const uint16_t port_;
    uint32_t next_call_id_;
    std::unordered_map<uint32_t, std::function<void (const msgpack::object&)>> ongoing_calls_;
    msgpack::unpacker unpacker_;
};

template <typename... ReturnsValueTypes, typename... ArgumentTypes>
std::tuple<ReturnsValueTypes...>  rpc_client::call(const std::string& procedure_name, ArgumentTypes... arguments) {
    std::future<std::tuple<ReturnsValueTypes...>> future =
            async_call(procedure_name, std::forward<ArgumentTypes>(arguments)...);
    future.wait();
    return future.get();
}

template <typename... ReturnValueTypes, typename... ArgumentTypes>
std::future<std::tuple<ReturnValueTypes...>> rpc_client::async_call(const std::string& procedure_name, ArgumentTypes... arguments) {
    if (!socket_.is_open()) {
        connect();
        read();
    }
    auto promise = std::make_shared<std::promise<std::tuple<ReturnValueTypes...>>>();
    const auto func = [promise](const msgpack::object& obj) {
        try {
            promise->set_value(obj.as<std::tuple<ReturnValueTypes...>>());
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    const auto call_id = next_call_id();
    ongoing_calls_[call_id] = std::move(func);
    auto buffer = std::make_shared<msgpack::sbuffer>();
    msgpack::pack(*buffer, std::make_tuple(call_id, procedure_name, std::make_tuple(arguments...)));
    boost::asio::async_write(socket_, boost::asio::buffer(buffer->data(), buffer->size()),
       [this, call_id, buffer](boost::system::error_code ec, std::size_t bytes_sent) {
           if (ec) {
               // TODO: Handle errors
           }
       });
    return promise->get_future();
}

#endif //RPC_RPC_CLIENT_H
