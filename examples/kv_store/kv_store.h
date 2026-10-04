#ifndef KV_STORE_H
#define KV_STORE_H

#include <boost/asio/strand.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <map>
#include <string>
#include <optional>
#include <vector>
#include <cassert>

class kv_store {
public:
    explicit kv_store(boost::asio::any_io_executor executor)
        : strand_{boost::asio::make_strand(executor)} {}

    boost::asio::awaitable<void> put(std::string key, std::string value) {
        return boost::asio::co_spawn(strand_, [this, key = std::move(key), value = std::move(value)]() mutable -> boost::asio::awaitable<void> {
            assert(strand_.running_in_this_thread());
            store_.insert_or_assign(std::move(key), std::move(value));
            co_return;
        }, boost::asio::use_awaitable);
    }

    boost::asio::awaitable<std::optional<std::string>> get(const std::string &key) const {
        return boost::asio::co_spawn(strand_, [this, key]() -> boost::asio::awaitable<std::optional<std::string>> {
            assert(strand_.running_in_this_thread());
            const auto it = store_.find(key);
            if (it == store_.end()) {
                co_return std::nullopt;
            }
            co_return it->second;
        }, boost::asio::use_awaitable);
    }

    boost::asio::awaitable<bool> erase(const std::string &key) {
        return boost::asio::co_spawn(strand_, [this, key]() -> boost::asio::awaitable<bool> {
            assert(strand_.running_in_this_thread());
            co_return store_.erase(key) > 0;
        }, boost::asio::use_awaitable);
    }

    boost::asio::awaitable<std::vector<std::string>> keys() const {
        return boost::asio::co_spawn(strand_, [this]() -> boost::asio::awaitable<std::vector<std::string>> {
            assert(strand_.running_in_this_thread());
            std::vector<std::string> keys;
            keys.reserve(store_.size());
            for (const auto &[key, value] : store_) {
                keys.push_back(key);
            }
            co_return keys;
        }, boost::asio::use_awaitable);
    }

private:
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::map<std::string, std::string> store_;
};

#endif  // KV_STORE_H
