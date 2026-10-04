#include <catch2/catch_test_macros.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <string>
#include <vector>
#include <thread>
#include <future>
#include <stdexcept>

#include "kv_store.h"

boost::asio::awaitable<void> worker(kv_store &store, int id, int iterations) {
    const std::string own_key = std::to_string(id);

    for (int i = 0; i < iterations; ++i) {
        const std::string expected = std::to_string(i);
        co_await store.put(own_key, expected);
        const auto value = co_await store.get(own_key);
        if (value != expected) {
            throw std::runtime_error("worker " + own_key + ": expected " + expected +
                                     ", got " + value.value_or("<missing>"));
        }

        if (i % 20 == 0) {
            co_await store.erase(own_key);
            if (co_await store.get(own_key)) {
                throw std::runtime_error("worker " + own_key + ": key still present after erase");
            }
            co_await store.put(own_key, expected);
        }

        if (i % 10 == 0) {
            co_await store.keys();
        }

        co_await store.put("shared", "value");
        co_await store.get("shared");
        co_await store.erase("shared");
    }
}

TEST_CASE("kv_store is safe under concurrent access", "[kv_store][stress]") {
    boost::asio::io_context io;

    kv_store store{io.get_executor()};

    constexpr int worker_count = 8;
    constexpr int iterations = 1000;

    std::vector<std::future<void>> results;
    for (int w = 0; w < worker_count; ++w) {      // spawn work first...
        results.push_back(boost::asio::co_spawn(io, worker(store, w, iterations), boost::asio::use_future));
    }
    {
        std::vector<std::jthread> threads;        // ...then start the threads
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&io] { io.run(); });
        }
    }                                             // joins: all work done

    for (auto &result : results) {
        REQUIRE_NOTHROW(result.get());            // rethrows worker failures here
    }

    auto keys_future = boost::asio::co_spawn(io, [&store]() -> boost::asio::awaitable<std::vector<std::string>> {
        auto result = co_await store.keys();
        co_return result;
    }, boost::asio::use_future);
    auto final_values = boost::asio::co_spawn(io, [&store]() -> boost::asio::awaitable<std::vector<std::optional<std::string>>> {
        std::vector<std::optional<std::string>> values;
        for (int w = 0; w < worker_count; ++w) {
            values.push_back(co_await store.get(std::to_string(w)));
        }
        co_return values;
    }, boost::asio::use_future);

    io.restart();
    io.run();

    const auto keys = keys_future.get();
    REQUIRE(keys.size() == worker_count);
    for (int w = 0; w < worker_count; ++w) {
        REQUIRE(std::find(keys.begin(), keys.end(), std::to_string(w)) != keys.end());
    }

    for (const auto &value : final_values.get()) {
        REQUIRE(value == std::to_string(iterations - 1));
    }

}
