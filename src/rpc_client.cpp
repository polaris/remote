#include "../include/remote/rpc_client.h"

rpc_client::rpc_client(boost::asio::io_service &io_service, const char* address, uint16_t port)
: io_service_{io_service}
, socket_{io_service}
, address_{address}
, port_{port}
, next_call_id_{0}
, buffer_{0} {
}

void rpc_client::connect() {
    boost::asio::ip::tcp::resolver resolver{io_service_};
    boost::asio::ip::tcp::resolver::iterator itr =
            resolver.resolve({address_, std::to_string(port_)});
    socket_.connect(*itr);
}

void rpc_client::read() {
    socket_.async_read_some(boost::asio::buffer(buffer_, 1024),
        [this](boost::system::error_code ec, std::size_t bytes_received) {
            if (!ec) {
                msgpack::object_handle result;
                auto b = reinterpret_cast<const char*>(buffer_);
                msgpack::unpack(result, b, bytes_received);
                const auto obj = result.get().as<std::tuple<uint32_t, msgpack::object>>();
                const auto call_id = std::get<0>(obj);
                if (ongoing_calls_.find(call_id) != ongoing_calls_.end()) {
                    ongoing_calls_[call_id](std::get<1>(obj));
                    ongoing_calls_.erase(call_id);
                }
                read();
            } else if (ec == boost::asio::error::eof) {
                // TODO: handle disconnect
            } else {
                // TODO: handle all other error
            }
        });
}
