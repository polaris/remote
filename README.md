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

Use `call_timeout` when creating the client to give every call a deadline without wrapping each one. Calls that exceed it fail with `remote::error::timed_out`:

```cpp
remote::client client{executor, {.call_timeout = std::chrono::seconds{5}}};
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
| `remote::error::message_too_large` | A response exceeded one of the client's [message limits](#message-limits). A request that exceeds the server's limits closes the connection instead. |
| `remote::error::timed_out` | No response within `remote::options::call_timeout`. The server may still have run the call. |
| `remote::error::msgid_in_use` | The message ID of the call is already in use. |
| `boost::asio::error::*` | Transport errors, and `operation_aborted` for cancelled calls. |

## Message limits

A client or server closes the connection when the peer sends a message that exceeds one of these limits:

| Limit | Default | Bounds |
|---|---|---|
| `remote::options::max_message_size` | 64 MiB | The size of a message in bytes |
| `remote::options::max_elements` | 1,048,576 | The number of elements in an array, or entries in a map |
| Nesting depth | 32 | The levels of nested arrays and maps, counting the message itself and a request's argument array |

What the caller sees depends on which side the limit was exceeded:

- **A response, on the client.** The calls pending on the connection fail with `remote::error::message_too_large`.
- **A request, on the server.** The server has no way to report the error, so it closes the connection. The client's calls on it fail with a transport error such as `boost::asio::error::eof`, as for any closed connection.

msgpack allocates storage for the elements of an array or map when it reads its header, before the elements arrive. One message can therefore make a connection reserve up to about 32 × `max_elements` × 48 bytes, 1.5 GiB with the defaults. Most of that is address space that is never touched: memory is only used as elements arrive, up to about 24 times the bytes received. When serving untrusted peers, lower `max_elements` and `max_message_size` to what your procedures need.

## Wire protocol

Messages are [msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md) over TCP:

```
request       [0, msgid, method, params]
response      [1, msgid, error, result]
notification  [2, method, params]
```

On failure, the server sends `[code, message]` as the error object, where `code` is a value of `remote::error`. The client accepts any error object; errors from other msgpack-rpc implementations are reported as `procedure_failed`.

## Building

Requirements: a C++20 compiler with coroutine support, CMake 3.24+ and Python 3. The dependencies are Boost (header-only parts), msgpack-cxx, and Catch2 for the tests.

The build tools, Conan and clang-tidy, are pinned in `requirements.txt`. Install them into a virtual environment:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Then build and test:

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
| `REMOTE_WARNINGS_AS_ERRORS` | off | Treat compiler warnings, and clang-tidy findings, as errors. Used by CI |
| `REMOTE_CLANG_TIDY` | off | Run clang-tidy on every source file while building |

### clang-tidy

The checks are configured in [`.clang-tidy`](.clang-tidy). To run them as part of a build, enable `REMOTE_CLANG_TIDY`. CMake uses the clang-tidy from `.venv` if it exists:

```sh
cmake -S . -B build/tidy -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DREMOTE_CLANG_TIDY=ON
cmake --build build/tidy
```

To check without rebuilding, run it over the compilation database of an existing build:

```sh
run-clang-tidy.py -p build/Debug -quiet "$PWD/(src|tests|examples)/.*"
```

### Sanitizer builds

Use a separate build directory, reusing the toolchain file that `conan install` generated:

```sh
cmake -S . -B build/tsan -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug -DREMOTE_SANITIZERS=thread
cmake --build build/tsan
cd build/tsan && TSAN_OPTIONS=halt_on_error=1 ctest --output-on-failure
```

`halt_on_error=1` stops at the first data race. Without it, ThreadSanitizer reports the race and continues, and the corrupted state can hang the test instead of failing it. For `address,undefined`, use a second build directory.

CI builds and tests with GCC and Clang on Linux and with Apple Clang on macOS. It also runs the tests under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer, and runs clang-tidy with findings treated as errors.

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
