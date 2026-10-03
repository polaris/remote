#include <remote/remote.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <iostream>
#include <thread>

int main() {
    constexpr remote::procedure<int(int, int)> add{"add"};

    boost::asio::io_context io;
    auto work = boost::asio::make_work_guard(io);
    std::thread io_thread{[&io] { io.run(); }};

    remote::server server{io.get_executor(), {boost::asio::ip::address_v4::loopback(), 0}};
    server.add_procedure(add, [](int a, int b) { return a + b; });
    server.start();

    remote::client client{io.get_executor()};
    client.async_connect(server.local_endpoint(), boost::asio::use_future).get();
    const int sum = client.call(add(1, 2));
    std::cout << "1 + 2 = " << sum << std::endl;

    client.close();
    server.stop();
    work.reset();
    io.stop();
    io_thread.join();
    return sum == 3 ? 0 : 1;
}
