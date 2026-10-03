# remote

[![CI](https://github.com/polaris/remote/actions/workflows/ci.yml/badge.svg)](https://github.com/polaris/remote/actions/workflows/ci.yml)

Typed [msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md) for C++20, built on
Boost.Asio coroutines.

Declare a procedure once in a header shared by client and server. Both sides are then checked
against the same signature at compile time. You don't need an IDL or code generator.

```cpp
// api.h
inline constexpr remote::procedure<int(int, int)> add{"add"};

// server
server.add_procedure(add, [](int a, int b) { return a + b; });

// client
int sum = co_await client.async_call(add(1, 2));
```

## Features

- **Type-safe procedures.** Arguments and results are checked at compile time on both ends.
  Any type msgpack can serialize works, including standard containers, `std::optional`,
  `std::tuple`, and your own structs via `MSGPACK_DEFINE`.
- **Asio-native API.** Every asynchronous operation takes a completion token, so you can use
  `co_await`, futures, or callbacks. The API supports per-operation cancellation, including timeouts
  with `||`.
- **Asynchronous handlers.** A server handler can return `boost::asio::awaitable<T>` and wait on
  I/O itself. Requests on one connection run concurrently, and their responses go out as each
  handler finishes.
- **Pipelining.** Many calls can be in flight on one connection at the same time.
- **Interoperable.** The wire format is plain msgpack-rpc, so implementations in other
  languages can talk to it.
- **Robust against bad input.** A malformed or oversized message closes only the offending
  connection.

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

// With a timeout, using boost::asio::experimental::awaitable_operators
auto result = co_await (client.async_call(add(1, 2), use_awaitable) || timer.async_wait(use_awaitable));

// Notification: no response, no error reporting
client.notify(log_event("started"));
```

### Asynchronous handlers

```cpp
inline constexpr remote::procedure<std::string(std::string)> fetch{"fetch"};

server.add_procedure(fetch, [&](std::string key) -> boost::asio::awaitable<std::string> {
    co_return co_await backend.async_lookup(key, boost::asio::use_awaitable);
});
```

## Errors

Failures are thrown as `boost::system::system_error`. The error code tells you what went
wrong:

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

Messages are [msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md) over
TCP:

```
request       [0, msgid, method, params]
response      [1, msgid, error, result]
notification  [2, method, params]
```

On failure, a remote server sends `[code, message]` as the error object, where `code` is a value of
`remote::error`. A remote client accepts any error object; errors from other implementations
are reported as `procedure_failed`.

## Building

Requirements: a C++20 compiler with coroutine support, CMake 3.24+ and Conan 2. The dependencies are Boost (header-only parts), msgpack-cxx, and Catch2 for the
tests.

```sh
conan install . --build=missing -s build_type=Debug -s compiler.cppstd=20
cmake --preset conan-debug
cmake --build --preset conan-debug
ctest --preset conan-debug
```

CMake options:

| Option | Default | |
|---|---|---|
| `REMOTE_BUILD_TESTS` | on if top-level | Build the Catch2 test suite |
| `REMOTE_BUILD_EXAMPLES` | on if top-level | Build the examples |
| `REMOTE_SANITIZERS` | empty | For example `address,undefined` or `thread` |
| `REMOTE_WARNINGS_AS_ERRORS` | off | Used by CI |

CI builds and tests with GCC and Clang on Linux and with Apple Clang on macOS. It also
runs the tests under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

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

- **One strand per connection.** Each connection, on both client and server, runs on its own
  strand. Its reads, writes and bookkeeping are serialized without locks. Handlers for
  different server connections can run in parallel if the `io_context` runs on several
  threads, so handlers that share state must synchronize.
- **Write queue.** Outgoing messages are queued, and only one `async_write` per socket is in
  flight at a time. A message stays queued until its write completes.
- **Coroutines throughout.** The accept loop, connection read loops and each request are
  coroutines. A pending call waits on a timer that the response reader cancels, which also
  gives per-operation cancellation for free.
- **Eager serialization.** `add(1, 2)` serializes the arguments immediately, so the arguments
  never need to outlive an asynchronous call.

## Limitations

- No backpressure: a peer that sends requests faster than it reads responses makes the
  server buffer them.
- No TLS.
- The client ignores requests and notifications sent by the server.

## License

MIT
