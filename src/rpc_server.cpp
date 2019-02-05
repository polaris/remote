#include "../include/remote/rpc_server.h"

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
                // TODO: Log error message
            }
            accept();
        });
}

rpc_server::session::session(boost::asio::ip::tcp::socket socket, rpc_server& server)
: socket_{std::move(socket)}
, server_{server} {
    unpacker_.reserve_buffer(1024);
}

void rpc_server::session::start() {
    read();
}

void rpc_server::session::read() {
    auto self = shared_from_this();
    socket_.async_read_some(boost::asio::buffer(unpacker_.buffer(), 1024), [self](boost::system::error_code ec, std::size_t bytes_received) {
        if (!ec) {
            self->unpacker_.buffer_consumed(bytes_received);
            msgpack::unpacked result;
            while (self->unpacker_.next(result)) {
                std::shared_ptr<msgpack::sbuffer> buffer = self->handle_call(result);
                self->write(buffer);
            }
            self->read();
        } else if (ec == boost::asio::error::eof) {
            self->socket_.close();
        } else {
            // TODO: Log error message
        }
    });
}

std::shared_ptr<msgpack::sbuffer> rpc_server::session::handle_call(const msgpack::unpacked &result) {
    std::shared_ptr<msgpack::sbuffer> buffer;
    const auto obj = result.get().as<std::tuple<uint32_t, std::string, msgpack::object>>();
    const auto call_id = std::get<0>(obj);
    const auto& procedure_name = std::get<1>(obj);
    const auto& args = std::get<2>(obj);
    try {
        buffer = server_.handlers_[procedure_name](call_id, args);
    } catch (const std::exception& ex) {
        buffer = report_error(call_id, ex);
    }
    return buffer;
}

std::shared_ptr<msgpack::sbuffer> rpc_server::session::report_error(unsigned int call_id, const std::exception &ex) const {
    auto buffer = std::make_shared<msgpack::sbuffer>();
    const std::string error_msg = ex.what();
    msgpack::pack(*buffer, make_tuple(call_id, false, error_msg));
    return buffer;
}

void rpc_server::session::write(const std::shared_ptr<msgpack::sbuffer>& buffer) {
    auto self = shared_from_this();
    boost::asio::async_write(socket_, boost::asio::buffer(buffer->data(), buffer->size()),
       [self, buffer](boost::system::error_code ec, std::size_t bytes_sent) {
           if (ec) {
               // TODO: Log error message
           }
       });
}
