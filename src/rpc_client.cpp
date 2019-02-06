#include "../include/remote/rpc_client.h"

namespace remote {

rpc_client::rpc_client(boost::asio::io_service &io_service, const char *address, uint16_t port)
        : io_service_{io_service}
        , socket_{io_service}
        , address_{address}
        , port_{port}
        , next_call_id_{0} {
    unpacker_.reserve_buffer(1024);
}

void rpc_client::connect() {
    boost::asio::ip::tcp::resolver resolver{io_service_};
    boost::asio::ip::tcp::resolver::iterator itr =
            resolver.resolve({address_, std::to_string(port_)});
    socket_.connect(*itr);
}

void rpc_client::read() {
    socket_.async_read_some(boost::asio::buffer(unpacker_.buffer(), 1024),
                            [this](boost::system::error_code ec, std::size_t bytes_received) {
                                if (!ec) {
                                    unpacker_.buffer_consumed(bytes_received);
                                    msgpack::unpacked result;
                                    while (unpacker_.next(result)) {
                                        const auto obj = result.get().as<std::tuple<uint32_t, bool, msgpack::object>>();
                                        const auto call_id = std::get<0>(obj);
                                        const auto success = std::get<1>(obj);
                                        if (ongoing_calls_.find(call_id) != ongoing_calls_.end()) {
                                            ongoing_calls_[call_id]->response_handler(success, std::get<2>(obj));
                                            ongoing_calls_.erase(call_id);
                                        } else {
                                            // TODO: Log error message
                                        }
                                    }
                                    read();
                                } else if (ec == boost::asio::error::eof) {
                                    socket_.close();
                                } else {
                                    // TODO: Log error message
                                }
                            });
}

void rpc_client::write(const std::shared_ptr<detail::call_t> &call) {
    queue_.emplace_back(call);
    if (queue_.size() == 1) {
        send_next_call();
    }
}

void rpc_client::send_next_call() {
    if (queue_.empty()) {
        return;
    }

    auto next_call = queue_.front();
    queue_.pop_front();
    const uint32_t call_id = next_call->call_id;
    ongoing_calls_[next_call->call_id] = next_call;
    boost::asio::async_write(socket_, boost::asio::buffer(next_call->buffer.data(), next_call->buffer.size()),
                             [this, call_id](boost::system::error_code ec, std::size_t bytes_sent) {
                                 ongoing_calls_[call_id]->write_handler(ec, bytes_sent);
                                 if (ec) {
                                     ongoing_calls_.erase(call_id);
                                 }
                                 send_next_call();
                             });
}

}   // namespace remote
