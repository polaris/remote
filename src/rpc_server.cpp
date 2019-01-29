#include "../include/rpc/rpc_server.h"

#include <iostream>
#include <tuple>

rpc_server::rpc_server(boost::asio::io_service &io_service, const char* address, uint16_t port)
: acceptor_{io_service} {
    boost::asio::ip::tcp::resolver resolver{io_service};
    boost::asio::ip::tcp::endpoint endpoint = *resolver.resolve(address, std::to_string(port)).begin();
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
    accept();
}

void rpc_server::accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!acceptor_.is_open()) {
                return;
            }
            if (!ec) {
                auto new_session = std::make_shared<session>(std::move(socket), *this);
                new_session->start();
            } else {
                // TODO: Handle errors
            }
            accept();
        });
}

rpc_server::session::session(boost::asio::ip::tcp::socket socket, rpc_server& server)
: socket_{std::move(socket)}
, server_{server}
, buffer_{0} {
}

void rpc_server::session::start() {
    read();
}

void rpc_server::session::read() {
    auto self = shared_from_this();
    socket_.async_receive(boost::asio::buffer(buffer_, 1024), [self](boost::system::error_code ec, std::size_t bytes_received) {
        if (!ec) {
            msgpack::object_handle result1, result2, result3;
            std::size_t off = 0;
            msgpack::unpack(result1, reinterpret_cast<const char*>(self->buffer_), bytes_received, off);
            msgpack::unpack(result2, reinterpret_cast<const char*>(self->buffer_), bytes_received, off);
            msgpack::unpack(result3, reinterpret_cast<const char*>(self->buffer_), bytes_received, off);
            const auto call_id = result1.get().as<uint32_t>();
            const auto procedure_name = result2.get().as<std::string>();
            const auto buffer = self->server_.handlers_[procedure_name](call_id, result3.get());
            self->write(buffer);
        } else if (ec == boost::asio::error::eof) {
            // TODO: Handle errors
        } else {
            // TODO: Handle errors
        }
    });
}

void rpc_server::session::write(const std::shared_ptr<msgpack::sbuffer>& buffer) {
    auto self = shared_from_this();
    socket_.async_send(boost::asio::buffer(buffer->data(), buffer->size()),
       [self, buffer](boost::system::error_code ec, std::size_t bytes_sent) {
           if (!ec) {
               self->read();
           } else {
               // TODO: Handle errors
           }
       });
}
