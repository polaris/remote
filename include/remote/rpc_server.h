#ifndef RPC_RPC_SERVER_H
#define RPC_RPC_SERVER_H

#include "detail/apply_from_tuple.h"

#include <msgpack.hpp>

#include <boost/asio.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace remote {

class rpc_server {
public:
    rpc_server(boost::asio::io_service &io_service, const char *address, uint16_t port);

    template<typename... ReturnValueTypes, typename... ArgumentTypes>
    void
    add_procedure(const char *procedure_name, std::function<std::tuple<ReturnValueTypes...>(ArgumentTypes...)> handler);

private:
    void accept();

    boost::asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<std::string, std::function<std::shared_ptr<msgpack::sbuffer>(uint32_t,
                                                                                    const msgpack::object &)>> handlers_;

    class session : public std::enable_shared_from_this<session> {
    public:
        session(boost::asio::ip::tcp::socket socket, rpc_server &server);

        ~session() = default;

        void start();

    private:
        void read();

        void write(const std::shared_ptr<msgpack::sbuffer> &buffer);

        std::shared_ptr<msgpack::sbuffer> handle_call(const msgpack::unpacked &result);

        std::shared_ptr<msgpack::sbuffer> report_error(unsigned int call_id, const std::exception &ex) const;

        boost::asio::ip::tcp::socket socket_;
        rpc_server &server_;
        msgpack::unpacker unpacker_;
    };
};

template<typename... ReturnValueTypes, typename... ArgumentTypes>
void rpc_server::add_procedure(const char *procedure_name,
                               std::function<std::tuple<ReturnValueTypes...>(ArgumentTypes...)> handler) {
    handlers_[procedure_name] = [handler](uint32_t call_id, const msgpack::object &args) {
        auto buffer = std::make_shared<msgpack::sbuffer>();
        const auto result = detail::apply_from_tuple(handler, args.as<std::tuple<ArgumentTypes...>>());
        msgpack::pack(*buffer, std::make_tuple(call_id, true, result));
        return buffer;
    };
}

}   // namespace remote

#endif //RPC_RPC_SERVER_H
