#include <remote/remote.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

namespace asio = boost::asio;
using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

namespace remote::detail {

// Reaches into a client for tests; the friend declarations are in client.h.
struct client_test_access {
    static void set_next_msgid(client &c, std::uint32_t msgid) {
        c.impl_->next_msgid_.store(msgid, std::memory_order_relaxed);
    }
};

}   // namespace remote::detail

namespace {

// The procedures used by the tests, declared once for client and server.
namespace api {

struct log_entry {
    std::uint64_t term = 0;
    std::uint64_t index = 0;
    std::string command;
    MSGPACK_DEFINE(term, index, command);
};

struct append_entries_request {
    std::uint64_t term = 0;
    std::uint32_t leader_id = 0;
    std::uint64_t prev_log_index = 0;
    std::uint64_t prev_log_term = 0;
    std::vector<log_entry> entries;
    std::uint64_t leader_commit = 0;
    MSGPACK_DEFINE(term, leader_id, prev_log_index, prev_log_term, entries, leader_commit);
};

struct append_entries_response {
    std::uint64_t term = 0;
    bool success = false;
    MSGPACK_DEFINE(term, success);
};

inline constexpr remote::procedure<int(int, int)> add{"add"};
inline constexpr remote::procedure<std::string(const std::string &)> echo{"echo"};
inline constexpr remote::procedure<void()> ping{"ping"};
inline constexpr remote::procedure<std::tuple<int, std::string>(int)> describe{"describe"};
inline constexpr remote::procedure<std::optional<std::string>(int)> lookup{"lookup"};
inline constexpr remote::procedure<int(std::string)> fail{"fail"};
inline constexpr remote::procedure<int(int)> delay{"delay"};
inline constexpr remote::procedure<void(int)> record{"record"};
inline constexpr remote::procedure<append_entries_response(append_entries_request)> append_entries{"append_entries"};

}   // namespace api

// Throws if the test does not finish in time instead of hanging the test run.
constexpr auto test_timeout = 10s;

struct fixture {
    explicit fixture(const remote::options &server_options = {}, const remote::options &client_options = {})
            : server{io.get_executor(), {asio::ip::address_v4::loopback(), 0}, server_options}
            , client{io.get_executor(), client_options} {
        server.add_procedure(api::add, [](int a, int b) { return a + b; });
        server.add_procedure(api::echo, [](const std::string &s) { return s; });
        server.add_procedure(api::ping, [] {});
        server.add_procedure(api::describe, [](int i) { return std::tuple{i, std::to_string(i)}; });
        server.add_procedure(api::lookup, [](int i) -> std::optional<std::string> {
            if (i == 0) {
                return std::nullopt;
            }
            return std::to_string(i);
        });
        server.add_procedure(api::fail, [](const std::string &message) -> int {
            throw std::runtime_error{message};
        });
        server.add_procedure(api::delay, [](int milliseconds) -> asio::awaitable<int> {
            asio::steady_timer timer{co_await asio::this_coro::executor, std::chrono::milliseconds{milliseconds}};
            co_await timer.async_wait(asio::use_awaitable);
            co_return milliseconds;
        });
        server.add_procedure(api::record, [this](int value) { recorded.push_back(value); });
        server.add_procedure(api::append_entries, [](const api::append_entries_request &req) {
            return api::append_entries_response{.term = req.term, .success = req.entries.size() == 2};
        });
        server.start();
    }

    /// Runs a coroutine on the io_context until it completes and returns its result.
    template<typename T>
    T run(asio::awaitable<T> task) {
        // Only assigned when T is not void.
        std::optional<std::conditional_t<std::is_void_v<T>, int, T>> result;  // NOLINT(misc-const-correctness)
        std::exception_ptr error;
        bool done = false;
        asio::co_spawn(io, std::move(task), [&](const std::exception_ptr &ex, [[maybe_unused]] auto &&... value) {
            error = ex;
            if constexpr (!std::is_void_v<T>) {
                if (!ex) {
                    result.emplace(std::move(value)...);
                }
            }
            done = true;
            io.stop();
        });
        io.restart();
        io.run_for(test_timeout);
        if (!done) {
            throw std::runtime_error{"test timed out"};
        }
        if (error) {
            std::rethrow_exception(error);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*result);
        }
    }

    void connect() {
        run(client.async_connect(server.local_endpoint(), asio::use_awaitable));
    }

    asio::io_context io;
    remote::server server;
    remote::client client;
    std::vector<int> recorded;
};

boost::system::error_code error_of(const std::function<void()> &f) {
    try {
        f();
    } catch (const boost::system::system_error &ex) {
        return ex.code();
    }
    return {};
}

// Completes with the error code of a call instead of throwing it.
asio::awaitable<boost::system::error_code> error_code_of(asio::awaitable<int> call) {
    try {
        co_await std::move(call);
    } catch (const boost::system::system_error &ex) {
        co_return ex.code();
    }
    co_return boost::system::error_code{};
}

boost::system::error_code send_raw(fixture &f, const std::vector<std::uint8_t> &bytes) {
    return f.run([&]() -> asio::awaitable<boost::system::error_code> {
        asio::ip::tcp::socket socket{co_await asio::this_coro::executor};
        co_await socket.async_connect(f.server.local_endpoint(), asio::use_awaitable);
        co_await asio::async_write(socket, asio::buffer(bytes), asio::use_awaitable);
        std::array<char, 16> buffer{};
        auto [ec, n] = co_await socket.async_read_some(asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));
        co_return ec;
    }());
}

}   // namespace

TEST_CASE_METHOD(fixture, "calls return the result of the handler", "[client][server]") {
    connect();

    CHECK(run(client.async_call(api::add(2, 3), asio::use_awaitable)) == 5);
    CHECK(run(client.async_call(api::echo("hello"), asio::use_awaitable)) == "hello");
    CHECK(run(client.async_call(api::describe(7), asio::use_awaitable)) == std::tuple{7, std::string{"7"}});
    CHECK(run(client.async_call(api::lookup(3), asio::use_awaitable)) == std::optional<std::string>{"3"});
    CHECK(run(client.async_call(api::lookup(0), asio::use_awaitable)) == std::nullopt);
    CHECK_NOTHROW(run(client.async_call(api::ping(), asio::use_awaitable)));
}

TEST_CASE_METHOD(fixture, "calls use the default completion token", "[client]") {
    connect();

    const int sum = run([this]() -> asio::awaitable<int> {
        co_return co_await client.async_call(api::add(20, 22));
    }());
    CHECK(sum == 42);
}

TEST_CASE_METHOD(fixture, "calls accept completion callbacks", "[client]") {
    connect();

    std::optional<int> sum;
    client.async_call(api::add(1, 2), [&](const std::exception_ptr &ex, int result) {
        CHECK(!ex);
        sum = result;
        io.stop();
    });
    io.restart();
    io.run_for(test_timeout);
    CHECK(sum == 3);
}

TEST_CASE_METHOD(fixture, "user-defined types are serialized", "[client][server]") {
    connect();

    const api::append_entries_request request{
            .term = 3,
            .leader_id = 1,
            .prev_log_index = 10,
            .prev_log_term = 2,
            .entries = {{.term = 3, .index = 11, .command = "set x 1"},
                        {.term = 3, .index = 12, .command = "set y 2"}},
            .leader_commit = 10,
    };
    const auto response = run(client.async_call(api::append_entries(request), asio::use_awaitable));
    CHECK(response.term == 3);
    CHECK(response.success);
}

TEST_CASE_METHOD(fixture, "calls fail when the client is not connected", "[client]") {
    CHECK(error_of([&] { run(client.async_call(api::add(1, 2), asio::use_awaitable)); })
          == remote::error::not_connected);
}

TEST_CASE_METHOD(fixture, "clients connect by host name and service", "[client]") {
    run(client.async_connect("localhost", std::to_string(server.local_endpoint().port()), asio::use_awaitable));
    CHECK(run(client.async_call(api::add(1, 1), asio::use_awaitable)) == 2);
}

TEST_CASE_METHOD(fixture, "unknown procedures are reported", "[server]") {
    connect();

    constexpr remote::procedure<void()> missing{"missing"};
    CHECK(error_of([&] { run(client.async_call(missing(), asio::use_awaitable)); }) == remote::error::unknown_procedure);
}

TEST_CASE_METHOD(fixture, "arguments that do not match the handler are reported", "[server]") {
    connect();

    SECTION("wrong type") {
        constexpr remote::procedure<int(std::string, std::string)> add_strings{"add"};
        CHECK(error_of([&] { run(client.async_call(add_strings("a", "b"), asio::use_awaitable)); })
              == remote::error::invalid_arguments);
    }
    SECTION("wrong number of arguments") {
        constexpr remote::procedure<int(int, int, int)> add_three{"add"};
        CHECK(error_of([&] { run(client.async_call(add_three(1, 2, 3), asio::use_awaitable)); })
              == remote::error::invalid_arguments);
    }
}

TEST_CASE_METHOD(fixture, "results that do not match the declared type are reported", "[client]") {
    connect();

    constexpr remote::procedure<std::string(int, int)> add_as_string{"add"};
    CHECK(error_of([&] { run(client.async_call(add_as_string(1, 2), asio::use_awaitable)); })
          == remote::error::invalid_result);
}

TEST_CASE_METHOD(fixture, "exceptions thrown by handlers are reported with their message", "[server]") {
    connect();

    try {
        run(client.async_call(api::fail("out of disk space"), asio::use_awaitable));
        FAIL("call did not throw");
    } catch (const boost::system::system_error &ex) {
        CHECK(ex.code() == remote::error::procedure_failed);
        CHECK_THAT(ex.what(), Catch::Matchers::ContainsSubstring("out of disk space"));
    }

    // The connection is still usable.
    CHECK(run(client.async_call(api::add(1, 2), asio::use_awaitable)) == 3);
}

TEST_CASE_METHOD(fixture, "responses of asynchronous handlers arrive as they complete", "[client][server]") {
    connect();

    std::vector<int> completed;
    auto call_delay = [&](int milliseconds) -> asio::awaitable<void> {
        completed.push_back(co_await client.async_call(api::delay(milliseconds), asio::use_awaitable));
    };
    run([&]() -> asio::awaitable<void> {
        co_await (call_delay(300) && call_delay(10));
    }());
    CHECK(completed == std::vector{10, 300});
}

TEST_CASE_METHOD(fixture, "many concurrent calls are pipelined over one connection", "[client][server]") {
    connect();

    constexpr int call_count = 1000;
    using operation = decltype(client.async_call(api::add(0, 0), asio::deferred));
    std::vector<operation> operations;
    operations.reserve(call_count);
    for (int i = 0; i < call_count; ++i) {
        operations.push_back(client.async_call(api::add(i, i), asio::deferred));
    }
    const auto [order, errors, results] = run(asio::experimental::make_parallel_group(std::move(operations))
                                                      .async_wait(asio::experimental::wait_for_all(),
                                                                  asio::use_awaitable));
    for (int i = 0; i < call_count; ++i) {
        REQUIRE(!errors[static_cast<std::size_t>(i)]);
        REQUIRE(results[static_cast<std::size_t>(i)] == 2 * i);
    }
}

TEST_CASE("blocking calls can be made from many threads", "[client][server]") {
    asio::io_context io;
    auto work = asio::make_work_guard(io);
    remote::server server{io.get_executor(), {asio::ip::address_v4::loopback(), 0}};
    server.add_procedure(api::add, [](int a, int b) { return a + b; });
    server.start();
    remote::client client{io.get_executor()};

    constexpr int io_thread_count = 2;
    std::vector<std::thread> io_threads;
    io_threads.reserve(io_thread_count);
    for (int i = 0; i < io_thread_count; ++i) {
        io_threads.emplace_back([&io] { io.run(); });
    }

    client.async_connect(server.local_endpoint(), asio::use_future).get();

    std::atomic<int> failures{0};
    constexpr int caller_count = 8;
    std::vector<std::thread> callers;
    callers.reserve(caller_count);
    for (int t = 0; t < caller_count; ++t) {
        callers.emplace_back([&client, &failures, t] {
            for (int i = 0; i < 100; ++i) {
                if (client.call(api::add(t, i)) != t + i) {
                    ++failures;
                }
            }
        });
    }
    for (auto &caller : callers) {
        caller.join();
    }
    CHECK(failures == 0);

    client.close();
    server.stop();
    work.reset();
    io.stop();
    for (auto &thread : io_threads) {
        thread.join();
    }
}

TEST_CASE("servers reject a procedure registered twice", "[server]") {
    asio::io_context io;
    remote::server server{io.get_executor(), {asio::ip::address_v4::loopback(), 0}};
    server.add_procedure(api::add, [](int a, int b) { return a + b; });
    CHECK_THROWS_WITH(server.add_procedure(api::add, [](int a, int b) { return a * b; }),
        Catch::Matchers::ContainsSubstring("already registered"));
}

TEST_CASE("servers reject a second start", "[server]") {
    asio::io_context io;
    remote::server server{io.get_executor(), {asio::ip::address_v4::loopback(), 0}};
    server.start();
    CHECK_THROWS_WITH(server.start(), Catch::Matchers::ContainsSubstring("already been started"));
}

TEST_CASE_METHOD(fixture, "servers reject procedures added after start", "[server]") {
    CHECK_THROWS_WITH(server.add_procedure(api::ping, [] {}),
        Catch::Matchers::ContainsSubstring("before the server is started"));
    connect();
    CHECK(run(client.async_call(api::add(2, 3), asio::use_awaitable)) == 5);
}

TEST_CASE_METHOD(fixture, "large messages span many reads", "[client][server]") {
    connect();

    const std::string large(std::size_t{4} * 1024 * 1024, 'x');
    CHECK(run(client.async_call(api::echo(large), asio::use_awaitable)) == large);
}

TEST_CASE("servers close connections that send oversized messages", "[server]") {
    fixture f{remote::options{.max_message_size = 1024}};
    f.connect();

    CHECK(f.run(f.client.async_call(api::echo("small"), asio::use_awaitable)) == "small");
    CHECK(error_of([&] { f.run(f.client.async_call(api::echo(std::string(4096, 'x')), asio::use_awaitable)); }));
}

TEST_CASE("servers close connections that stream an oversized value", "[server]") {
    fixture f{remote::options{.max_message_size = 1024}};

    // A single string that claims 1 MiB but is never completed. The unpacker consumes only its
    // header and buffers the payload, so the limit must also count buffered bytes.
    std::vector<std::uint8_t> bytes{0xdb, 0x00, 0x10, 0x00, 0x00};   // str32 of 1 MiB
    bytes.resize(bytes.size() + (std::size_t{8} * 1024), 'x');

    const auto ec = send_raw(f, bytes);
    CHECK((ec == asio::error::eof || ec == asio::error::connection_reset));
}

TEST_CASE("servers close connections that announce more elements than max_elements", "[server]") {
    // Headers for 17 elements that never follow. Without the limit, the server would allocate
    // storage for them and wait for the elements instead of closing the connection. The count
    // is kept small so that the allocation succeeds on every platform: a huge one could fail
    // with bad_alloc, which also closes the connection and would hide a missing limit.
    const auto bytes = GENERATE(std::vector<std::uint8_t>{0xdc, 0x00, 0x11},   // array16 of 17
                                std::vector<std::uint8_t>{0xde, 0x00, 0x11});  // map16 of 17
    fixture f{remote::options{.max_elements = 16}};
    const auto ec = send_raw(f, bytes);
    CHECK((ec == asio::error::eof || ec == asio::error::connection_reset));
}

TEST_CASE("servers close connections that send values nested deeper than the depth limit", "[server]") {
    std::vector<std::uint8_t> bytes{0x94, 0x00, 0x01, 0xa1, 'x'};   // [0, 1, "x",
    bytes.insert(bytes.end(), 100, 0x91);                            //   [[[[...
    bytes.push_back(0xc0);                                           //   nil ]]]]
    fixture f;
    const auto ec = send_raw(f, bytes);
    CHECK((ec == asio::error::eof || ec == asio::error::connection_reset));
}

TEST_CASE("servers close connections that send arrays with more than max_elements", "[server]") {
    fixture f{remote::options{.max_elements = 4}};
    f.connect();
    constexpr remote::procedure<void(std::vector<int>)> sum{"sum"};

    // Four elements are allowed: the request parses and reaches the method lookup.
    CHECK(error_of([&] { f.run(f.client.async_call(sum({1, 2, 3, 4}), asio::use_awaitable)); })
          == remote::error::unknown_procedure);
    // Five are not: the server closes the connection without a response.
    const auto ec = error_of([&] { f.run(f.client.async_call(sum({1, 2, 3, 4, 5}), asio::use_awaitable)); });
    CHECK(ec);
    CHECK(ec != remote::error::unknown_procedure);
}

TEST_CASE("clients reject oversized responses", "[client]") {
    fixture f{remote::options{}, remote::options{.max_message_size = 1024}};
    f.connect();

    CHECK(error_of([&] { f.run(f.client.async_call(api::echo(std::string(4096, 'x')), asio::use_awaitable)); })
          == remote::error::message_too_large);
}

TEST_CASE_METHOD(fixture, "late responses to cancelled calls are discarded", "[client]") {
    connect();

    const auto outcome = run([&]() -> asio::awaitable<std::variant<int, std::monostate>> {
        asio::steady_timer timeout{co_await asio::this_coro::executor, 20ms};
        co_return co_await (client.async_call(api::delay(200), asio::use_awaitable)
                            || timeout.async_wait(asio::use_awaitable));
    }());
    CHECK(outcome.index() == 1);

    // The response to the cancelled call arrives while this call is pending and is discarded.
    CHECK(run(client.async_call(api::delay(400), asio::use_awaitable)) == 400);
}

TEST_CASE_METHOD(fixture, "closing the client fails pending calls", "[client]") {
    connect();

    const auto ec = error_of([&] {
        run([&]() -> asio::awaitable<void> {
            asio::steady_timer timer{co_await asio::this_coro::executor, 20ms};
            co_await (client.async_call(api::delay(5000), asio::use_awaitable)
                      && [&]() -> asio::awaitable<void> {
                             co_await timer.async_wait(asio::use_awaitable);
                             client.close();
                         }());
        }());
    });
    CHECK(ec == asio::error::operation_aborted);
}

TEST_CASE_METHOD(fixture, "stopping the server fails pending calls", "[client][server]") {
    connect();

    const auto ec = error_of([&] {
        run([&]() -> asio::awaitable<void> {
            asio::steady_timer timer{co_await asio::this_coro::executor, 20ms};
            co_await (client.async_call(api::delay(5000), asio::use_awaitable)
                      && [&]() -> asio::awaitable<void> {
                             co_await timer.async_wait(asio::use_awaitable);
                             server.stop();
                         }());
        }());
    });
    CHECK(ec);
}

TEST_CASE_METHOD(fixture, "clients can reconnect", "[client]") {
    connect();
    CHECK(run(client.async_call(api::add(1, 2), asio::use_awaitable)) == 3);

    client.close();
    connect();
    CHECK(run(client.async_call(api::add(3, 4), asio::use_awaitable)) == 7);
}

TEST_CASE_METHOD(fixture, "notifications invoke the handler without a response", "[client][server]") {
    connect();

    client.notify(api::record(1));
    client.notify(api::record(2));
    // Messages on one connection are handled in order, so the notifications have been
    // processed once this call returns.
    run(client.async_call(api::ping(), asio::use_awaitable));
    CHECK(recorded == std::vector{1, 2});
}

TEST_CASE_METHOD(fixture, "malformed input closes only the offending connection", "[server]") {
    connect();

    const auto is_closed = [](const boost::system::error_code &ec) {
        return ec == asio::error::eof || ec == asio::error::connection_reset;
    };

    SECTION("bytes that are not msgpack") {
        // 0xc1 is never used in msgpack.
        CHECK(is_closed(send_raw(*this, {0xc1})));
    }
    SECTION("msgpack that is not msgpack-rpc") {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, std::tuple{"not", "rpc"});
        CHECK(is_closed(send_raw(*this, {buffer.data(), buffer.data() + buffer.size()})));
    }

    CHECK(run(client.async_call(api::add(1, 2), asio::use_awaitable)) == 3);
}

TEST_CASE("calls that respond within the call timeout succeed", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    CHECK(f.run(f.client.async_call(api::delay(1), asio::use_awaitable)) == 1);
}

TEST_CASE("calls that exceed the call timeout fail with timed_out", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    CHECK(error_of([&] { f.run(f.client.async_call(api::delay(500), asio::use_awaitable)); })
          == remote::error::timed_out);
}

TEST_CASE("late responses to timed-out calls are discarded", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    CHECK(error_of([&] { f.run(f.client.async_call(api::delay(300), asio::use_awaitable)); })
          == remote::error::timed_out);

    // The response to the timed-out call arrives about 200 ms from now. Keep calls in flight
    // until well past that, so that it arrives while another call is pending. Each call must
    // still receive its own result.
    const auto mismatches = f.run([&]() -> asio::awaitable<int> {
        int count = 0;
        const auto until = std::chrono::steady_clock::now() + 400ms;
        for (int milliseconds = 10; std::chrono::steady_clock::now() < until; ++milliseconds) {
            if (co_await f.client.async_call(api::delay(milliseconds), asio::use_awaitable) != milliseconds) {
                ++count;
            }
        }
        co_return count;
    }());
    CHECK(mismatches == 0);
}

TEST_CASE("non-positive and huge call timeouts disable the timeout", "[client][timeout]") {
    const auto timeout = GENERATE(0ms, -1ms, std::chrono::milliseconds::max());
    CAPTURE(timeout.count());
    fixture f{remote::options{}, remote::options{.call_timeout = timeout}};
    f.connect();

    CHECK(f.run(f.client.async_call(api::delay(300), asio::use_awaitable)) == 300);
}

TEST_CASE("calls cancelled by the caller report operation_aborted rather than timed_out", "[client][timeout]") {
    // The call timeout is longer than the caller's own deadline, so the call can only fail
    // because the caller cancelled it.
    fixture f{remote::options{}, remote::options{.call_timeout = 500ms}};
    f.connect();

    // Order of completion, then the call's results, then the timer's result.
    using race_result = std::tuple<std::array<std::size_t, 2>, std::exception_ptr, int, boost::system::error_code>;
    const race_result outcome = f.run([&]() -> asio::awaitable<race_result> {
        asio::steady_timer deadline{co_await asio::this_coro::executor, 20ms};
        // Unlike operator||, a parallel group reports the result of every operation,
        // including the one it cancelled.
        co_return co_await asio::experimental::make_parallel_group(
                f.client.async_call(api::delay(200), asio::deferred),
                deadline.async_wait(asio::deferred))
            .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);
    }());

    const auto &[order, call_error, call_result, timer_error] = outcome;
    CHECK(order[0] == 1);   // the caller's deadline fired first
    CHECK(!timer_error);
    REQUIRE(call_error);
    const std::exception_ptr error = call_error;
    CHECK(error_of([&] { std::rethrow_exception(error); }) == asio::error::operation_aborted);
}

TEST_CASE("with_timeout adds a timeout when none is configured", "[client][timeout]") {
    fixture f;
    f.connect();

    CHECK(error_of([&] {
              f.run(f.client.async_call(api::delay(300).with_timeout(100ms), asio::use_awaitable));
          }) == remote::error::timed_out);
}

TEST_CASE("with_timeout extends the configured timeout", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    CHECK(f.run(f.client.async_call(api::delay(300).with_timeout(1s), asio::use_awaitable)) == 300);
}

TEST_CASE("with_timeout of zero disables the configured timeout", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    CHECK(f.run(f.client.async_call(api::delay(300).with_timeout(0ms), asio::use_awaitable)) == 300);
}

TEST_CASE("with_timeout applies to blocking calls", "[client][timeout]") {
    fixture f{remote::options{}, remote::options{.call_timeout = 100ms}};
    f.connect();

    // call() blocks this thread until the response arrives, so another thread has to run the
    // io_context. The server's pending accept keeps run() busy until it is stopped.
    f.io.restart();
    const std::jthread io_thread{[&f] { f.io.run(); }};
    // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): local scope guard, never copied
    struct stop_on_exit {
        ~stop_on_exit() { io.stop(); }
        asio::io_context &io;
    };
    const stop_on_exit stopper{f.io};   // destroyed before io_thread, so the join below cannot hang

    CHECK(f.client.call(api::delay(300).with_timeout(1s)) == 300);
}

TEST_CASE_METHOD(fixture, "calls fail when their msgid is still in use", "[client]") {
    connect();

    using outcome = std::tuple<int, boost::system::error_code>;
    const auto [pending_result, clashing_error] = run([&]() -> asio::awaitable<outcome> {
        // Simulates the counter wrapping around while a call is pending: both calls get msgid 7.
        // The msgid is taken when async_call is called; the calls start when they are awaited.
        remote::detail::client_test_access::set_next_msgid(client, 7);
        auto pending = client.async_call(api::delay(100), asio::use_awaitable);
        remote::detail::client_test_access::set_next_msgid(client, 7);
        auto clashing = client.async_call(api::add(1, 2), asio::use_awaitable);
        co_return co_await (std::move(pending) && error_code_of(std::move(clashing)));
    }());

    CHECK(clashing_error == remote::error::msgid_in_use);
    CHECK(pending_result == 100);   // the pending call still receives its own result
}
