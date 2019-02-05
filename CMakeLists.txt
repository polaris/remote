cmake_minimum_required(VERSION 3.12)
project(remote)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Msgpack 3.1.1 REQUIRED)
find_package(Boost 1.69.0 REQUIRED)
find_package(Threads REQUIRED)

add_library(remote STATIC
        src/rpc_client.cpp
        include/remote/rpc_client.h
        src/rpc_server.cpp
        include/remote/rpc_server.h
        include/remote/detail/apply_from_tuple.h
        include/remote/detail/then.h)
target_compile_options(remote PRIVATE "-Wall")

add_executable(client example/client.cpp)
target_include_directories(client SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS})
target_link_libraries(client PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES})

add_executable(server example/server.cpp)
target_include_directories(server SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS})
target_link_libraries(server PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES})

install(TARGETS ${CMAKE_PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/remote DESTINATION include)
