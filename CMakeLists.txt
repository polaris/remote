cmake_minimum_required(VERSION 3.13)
project(rpc)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Msgpack 3.1.1 REQUIRED)
find_package(Boost 1.69.0 REQUIRED)
find_package(Threads REQUIRED)

add_library(rpc STATIC
        src/rpc_client.cpp
        include/rpc/rpc_client.h
        src/rpc_server.cpp
        include/rpc/rpc_server.h
        include/rpc/detail/apply_from_tuple.h)
target_compile_options(rpc PRIVATE "-Wall")

add_executable(client example/client.cpp)
target_include_directories(client SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS})
target_link_libraries(client PRIVATE rpc Threads::Threads ${MSGPACK_LIBRARIES})

add_executable(server example/server.cpp)
target_include_directories(server SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS})
target_link_libraries(server PRIVATE rpc Threads::Threads ${MSGPACK_LIBRARIES})
