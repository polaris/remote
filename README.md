# remote

[![CI](https://github.com/polaris/remote/actions/workflows/ci.yml/badge.svg)](https://github.com/polaris/remote/actions/workflows/ci.yml)

Typed [msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md) for C++20,
built on Boost.Asio coroutines.

Declare procedures in a header shared by client and server. Both sides are then checked against the same signature at compile time. No need of an IDL or a code generator.

```cpp
// api.h
inline constexpr remote::procedure<int(int, int)> add{"add"};

// server
server.add_procedure(add, [](int a, int b) { return a + b; });

// client
int sum = co_await client.async_call(add(1, 2));
```

## Features

- **Type-safe procedures.** Arguments and results are checked at compile time. Any type msgpack can serialize is supported, including standard containers, `std::optional`, `std::tuple`, and custom structs via `MSGPACK_DEFINE`.
- **Asio-native API.** Every asynchronous operation takes a completion token, so you can use `co_await`, futures, or callbacks. The API supports per-operation cancellation, including timeouts with `||`.
- **Asynchronous handlers.** A server handler can return `boost::asio::awaitable<T>` and wait on I/O itself. Requests on one connection run concurrently, and their responses go out as each handler finishes.
- **Pipelining.** Many calls can be in flight on one connection at the same time.
- **Interoperable.** The wire format is plain msgpack-rpc, so implementations in other languages can talk to it.
- **Robust against bad input.** A malformed or oversized message closes only the offending connection.

## Example

The [`examples/kv_store`](examples/kv_store) directory has a small key-value store:

```cpp
// kv_api.h - shared by server and client
namespace kv {
    inline constexpr remote::procedure<void(std::string, std::string)> put{"kv.put"};
    inline constexpr remote::procedure<std::optional<std::string>(std::string)> get{"kv.get"};
    inline constexpr remote::procedure<bool(std::string)> erase{"kv.erase"};
    inline constexpr remote::procedure<std::vector<std::string>()> keys{"kv.keys"};
}
```

```cpp
// Server
boost::asio::io_context io;
std::map<std::string, std::string> store;

remote::server server{io.get_executor(), {boost::asio::ip::tcp::v4(), 7070}};
server.add_procedure(kv::put, [&](std::string key, std::string value) {
    store.insert_or_assign(std::move(key), std::move(value));
});
server.add_procedure(kv::get, [&](const std::string &key) -> std::optional<std::string> {
    auto it = store.find(key);
    return it == store.end() ? std::nullopt : std::optional{it->second};
});
server.start();
io.run();
```

For the sake of brevity, the code snippet is executed in a single thread. The full example runs the `io_context` on a thread pool and keeps the map in [`kv_store`](examples/kv_store/kv_store.h), which serializes access with its own strand. A stress test under ThreadSanitizer checks that ([`test_kv_store.cpp`](examples/kv_store/test_kv_store.cpp)).

```cpp
// Client, in a coroutine
remote::client client{co_await boost::asio::this_coro::executor};
co_await client.async_connect("localhost", "7070");
co_await client.async_call(kv::put("greeting", "hello"));
std::optional<std::string> value = co_await client.async_call(kv::get("greeting"));
```

```console
$ ./kv_server 7070 &
$ ./kv_client localhost 7070 put greeting "hello world"
$ ./kv_client localhost 7070 get greeting
hello world
```

### Other ways to call

```cpp
// Blocking, from a thread that does not run the io_context
int sum = client.call(add(1, 2));

// Callback
client.async_call(add(1, 2), [](std::exception_ptr error, int sum) { /* ... */ });

// With a timeout, in a coroutine
// (operator|| comes from boost::asio::experimental::awaitable_operators)
boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, std::chrono::seconds{5}};
std::variant<int, std::monostate> result = co_await (client.async_call(add(1, 2), boost::asio::use_awaitable)
                                                     || timer.async_wait(boost::asio::use_awaitable));
// result.index() == 1 means the call timed out and was cancelled

// Notification: no response, no error reporting. log_event is declared in the shared header:
//     inline constexpr remote::procedure<void(std::string)> log_event{"log_event"};
client.notify(log_event("started"));
```

### Asynchronous handlers

A handler that returns `boost::asio::awaitable<T>` can wait without blocking the connection:

```cpp
inline constexpr remote::procedure<std::string(std::string)> delayed_echo{"delayed_echo"};

server.add_procedure(delayed_echo, [](std::string text) -> boost::asio::awaitable<std::string> {
    boost::asio::steady_timer timer{co_await boost::asio::this_coro::executor, std::chrono::seconds{1}};
    co_await timer.async_wait(boost::asio::use_awaitable);
    co_return text;
});
```

## Errors

Failures are thrown as `boost::system::system_error`. The error code tells you what went wrong:

| Code | Meaning |
|---|---|
| `remote::error::not_connected` | The client has no open connection. |
| `remote::error::unknown_procedure` | The server has no handler for the procedure. |
| `remote::error::invalid_arguments` | The arguments don't match the handler's signature. |
| `remote::error::procedure_failed` | The handler threw. `what()` contains its message. |
| `remote::error::invalid_result` | The result doesn't convert to the declared result type. |
| `remote::error::protocol_error` | The peer sent something that isn't msgpack-rpc. |
| `remote::error::message_too_large` | A message exceeded `remote::options::max_message_size`. |
| `boost::asio::error::*` | Transport errors, and `operation_aborted` for cancelled calls. |

## Wire protocol

Messages are [msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md) over TCP:

```
request       [0, msgid, method, params]
response      [1, msgid, error, result]
notification  [2, method, params]
```

On failure, the server sends `[code, message]` as the error object, where `code` is a value of `remote::error`. The client accepts any error object; errors from other msgpack-rpc implementations are reported as `procedure_failed`.

## Building

Requirements: a C++20 compiler with coroutine support, CMake 3.24+ and Conan 2. The dependencies are Boost (header-only parts), msgpack-cxx, and Catch2 for the tests.

```sh
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug
```

CMake options:

| Option | Default | Description |
|---|---|---|
| `REMOTE_BUILD_TESTS` | on if top-level | Build the Catch2 test suite |
| `REMOTE_BUILD_EXAMPLES` | on if top-level | Build the examples |
| `REMOTE_SANITIZERS` | empty | For example `address,undefined` or `thread` |
| `REMOTE_WARNINGS_AS_ERRORS` | off | Used by CI |

### Sanitizer builds

Use a separate build directory, reusing the toolchain file that `conan install` generated:

```sh
cmake -S . -B build/tsan -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DREMOTE_SANITIZERS=thread
cmake --build build/tsan
cd build/tsan && TSAN_OPTIONS=halt_on_error=1 ctest --output-on-failure
```

`halt_on_error=1` stops at the first data race. Without it, ThreadSanitizer reports the race and continues, and the corrupted state can hang the test instead of failing it. For `address,undefined`, use a second build directory.

CI builds and tests with GCC and Clang on Linux and with Apple Clang on macOS. It also runs the tests under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

### Using remote in your project

As a Conan package:

```sh
conan create . -s compiler.cppstd=20
```

```cmake
find_package(remote REQUIRED CONFIG)
target_link_libraries(app PRIVATE remote::remote)
```

`cmake --install` also installs a CMake package config, for use without Conan.

## Design

- **One strand per connection.** Each connection, on client and server, runs on its own strand. Its reads, writes and bookkeeping are serialized without locks. Handlers for different server connections can run in parallel if the `io_context` runs on several threads, so handlers that share state must synchronize. `kv_store` in the example shows one way, with a strand.
- **Write queue.** Outgoing messages are queued, and only one `async_write` per socket is in flight at a time. A message stays queued until its write completes.
- **Coroutines throughout.** The accept loop, connection read loops and each request are coroutines. A pending call waits on a timer that the response reader cancels, which also gives per-operation cancellation for free.
- **Eager serialization.** `add(1, 2)` serializes the arguments immediately, so the argument never need to outlive an asynchronous call.

## Limitations

- No backpressure: a peer that sends requests faster than it reads responses makes the server buffer them.
- No TLS.
- The client ignores requests and notifications sent by the server.

## License

MIT
