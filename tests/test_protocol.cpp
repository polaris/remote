#include <remote/error.h>
#include <remote/procedure.h>
#include <remote/detail/protocol.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <boost/system/system_error.hpp>

#include <string>
#include <tuple>

using remote::detail::message;
using remote::detail::parse_message;

namespace {

template<typename T>
msgpack::object_handle unpack(const T &value) {
    msgpack::sbuffer buffer;
    msgpack::pack(buffer, value);
    return msgpack::unpack(buffer.data(), buffer.size());
}

msgpack::object_handle unpack(const msgpack::sbuffer &buffer) {
    return msgpack::unpack(buffer.data(), buffer.size());
}

boost::system::error_code code_of(const std::exception_ptr &ex) {
    try {
        std::rethrow_exception(ex);
    } catch (const boost::system::system_error &error) {
        return error.code();
    }
}

std::string what_of(const std::exception_ptr &ex) {
    try {
        std::rethrow_exception(ex);
    } catch (const std::exception &error) {
        return error.what();
    }
}

}   // namespace

TEST_CASE("requests round-trip through the wire format", "[protocol]") {
    constexpr remote::procedure<void(int, std::string)> proc{"add"};
    const auto call = proc(1, "two");
    const auto handle = unpack(remote::detail::pack_request(42, call.method(), call.params()));
    const message msg = parse_message(handle.get());

    const auto *req = std::get_if<remote::detail::request>(&msg);
    REQUIRE(req != nullptr);
    CHECK(req->msgid == 42);
    CHECK(req->method == "add");
    CHECK(req->params.as<std::tuple<int, std::string>>() == std::tuple{1, std::string{"two"}});
}

TEST_CASE("requests without arguments carry an empty params array", "[protocol]") {
    constexpr remote::procedure<void()> proc{"ping"};
    const auto handle = unpack(remote::detail::pack_request(1, proc.name(), proc().params()));
    const message msg = parse_message(handle.get());

    const auto &req = std::get<remote::detail::request>(msg);
    CHECK(req.params.type == msgpack::type::ARRAY);
    CHECK(req.params.via.array.size == 0);
}

TEST_CASE("notifications round-trip through the wire format", "[protocol]") {
    constexpr remote::procedure<void(std::string)> proc{"log"};
    const auto handle = unpack(remote::detail::pack_notification(proc.name(), proc("hello").params()));
    const message msg = parse_message(handle.get());

    const auto *notif = std::get_if<remote::detail::notification>(&msg);
    REQUIRE(notif != nullptr);
    CHECK(notif->method == "log");
    CHECK(notif->params.as<std::tuple<std::string>>() == std::tuple{std::string{"hello"}});
}

TEST_CASE("results are embedded into responses", "[protocol]") {
    msgpack::sbuffer packed_result;
    msgpack::pack(packed_result, std::string{"result"});

    const auto handle = unpack(remote::detail::pack_result(7, packed_result));
    const message msg = parse_message(handle.get());

    const auto *resp = std::get_if<remote::detail::response>(&msg);
    REQUIRE(resp != nullptr);
    CHECK(resp->msgid == 7);
    CHECK(resp->error.is_nil());
    CHECK(resp->result.as<std::string>() == "result");
}

TEST_CASE("errors are carried as code and message", "[protocol]") {
    const auto handle = unpack(remote::detail::pack_error(7, remote::error::unknown_procedure, "no such thing"));
    const message msg = parse_message(handle.get());

    const auto &resp = std::get<remote::detail::response>(msg);
    CHECK(resp.result.is_nil());

    const std::exception_ptr ex = remote::detail::make_exception(resp.error);
    CHECK(code_of(ex) == remote::error::unknown_procedure);
    CHECK_THAT(what_of(ex), Catch::Matchers::ContainsSubstring("no such thing"));
}

TEST_CASE("errors from other msgpack-rpc implementations are reported as procedure_failed", "[protocol]") {
    SECTION("a plain string") {
        const auto handle = unpack(std::string{"something broke"});
        const std::exception_ptr ex = remote::detail::make_exception(handle.get());
        CHECK(code_of(ex) == remote::error::procedure_failed);
        CHECK_THAT(what_of(ex), Catch::Matchers::ContainsSubstring("something broke"));
    }
    SECTION("a code that remote does not send") {
        const auto handle = unpack(std::tuple{1, std::string{"validation failed"}});
        const std::exception_ptr ex = remote::detail::make_exception(handle.get());
        CHECK(code_of(ex) == remote::error::procedure_failed);
        CHECK_THAT(what_of(ex), Catch::Matchers::ContainsSubstring("validation failed"));
    }
    SECTION("an arbitrary object") {
        const auto handle = unpack(std::tuple{true, 3.5});
        const std::exception_ptr ex = remote::detail::make_exception(handle.get());
        CHECK(code_of(ex) == remote::error::procedure_failed);
    }
}

TEST_CASE("malformed messages are rejected", "[protocol]") {
    const auto rejects = [](const msgpack::object_handle &handle) {
        try {
            parse_message(handle.get());
        } catch (const boost::system::system_error &ex) {
            return ex.code() == remote::error::protocol_error;
        }
        return false;
    };

    CHECK(rejects(unpack(42)));
    CHECK(rejects(unpack(std::string{"request"})));
    CHECK(rejects(unpack(std::tuple<>{})));
    CHECK(rejects(unpack(std::tuple{3, 1, std::string{"x"}, std::tuple<>{}})));      // unknown type
    CHECK(rejects(unpack(std::tuple{-1, 1, std::string{"x"}, std::tuple<>{}})));     // negative type
    CHECK(rejects(unpack(std::tuple{0, 1, std::string{"x"}})));                     // request too short
    CHECK(rejects(unpack(std::tuple{0, -1, std::string{"x"}, std::tuple<>{}})));     // negative msgid
    CHECK(rejects(unpack(std::tuple{0, 1ULL << 40, std::string{"x"}, std::tuple<>{}})));  // msgid > 32 bit
    CHECK(rejects(unpack(std::tuple{0, 1, 2, std::tuple<>{}})));                    // method not a string
    CHECK(rejects(unpack(std::tuple{0, 1, std::string{"x"}, 5})));                  // params not an array
    CHECK(rejects(unpack(std::tuple{1, 1, msgpack::type::nil_t{}})));               // response too short
    CHECK(rejects(unpack(std::tuple{2, std::string{"x"}, std::tuple<>{}, 1})));      // notification too long
}

TEST_CASE("error codes have readable messages", "[protocol]") {
    const boost::system::error_code ec = remote::error::unknown_procedure;
    CHECK(ec.category().name() == std::string{"remote"});
    CHECK(ec.message() == "unknown procedure");
}
