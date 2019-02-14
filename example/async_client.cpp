#include "../include/remote/rpc_client.h"

#include <boost/asio.hpp>

#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>

std::future<void> t1;

class Requester {
public:
    explicit Requester(boost::asio::io_service& io_service)
            : io_service_{io_service}
            , client_{io_service, "127.0.0.1", 12345} {}

    void start(double d) {
        async_request(d);
    }

private:
    void async_request(double d) {
        io_service_.post([this, d](){
            remote::rpc_future<double> sin_result = client_.async_call<double>("sin", d);
            future = sin_result.then([this, d](std::future<std::tuple<double>>& res) {
                try {
                    const auto tuple = res.get();
                    std::cout << "Result: " << std::get<0>(tuple) << std::endl;
                    async_request(d + 0.01);
                } catch (const std::exception& ex) {
                    std::cout << "Failure: " << ex.what() << std::endl;
                }
            });
        });
    }

    boost::asio::io_service& io_service_;
    remote::rpc_client client_;
    std::future<void> future;
};

int main() {
    try {
        boost::asio::io_service io_service;

        Requester requester{io_service};
        requester.start(0.0);

        io_service.run();

    } catch (const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }

    return 0;
}
