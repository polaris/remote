cmake_minimum_required(VERSION 3.12)
project(remote)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)

find_package(Msgpack 3.1.1 REQUIRED)
find_package(Boost 1.68.0 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

add_library(remote STATIC
        src/rpc_client.cpp
        include/remote/rpc_client.h
        src/rpc_server.cpp
        include/remote/rpc_server.h
        include/remote/detail/apply_from_tuple.h
        include/remote/detail/call.h
        include/remote/detail/then.h)
target_include_directories(remote SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
target_compile_options(remote PRIVATE "-Wall")

add_executable(async_client example/async_client.cpp)
target_include_directories(async_client SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
target_link_libraries(async_client PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES} ${Boost_LIBRARIES})

add_executable(client example/client.cpp example/log_entries.h)
target_include_directories(client SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
target_link_libraries(client PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES} ${Boost_LIBRARIES})

add_executable(simple_call example/simple_call.cpp)
target_include_directories(simple_call SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
target_link_libraries(simple_call PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES} ${Boost_LIBRARIES})

add_executable(server example/server.cpp example/log_entries.h)
target_include_directories(server SYSTEM PRIVATE ${MSGPACK_INCLUDE_DIRS} ${Boost_INCLUDE_DIR})
target_link_libraries(server PRIVATE remote Threads::Threads ${MSGPACK_LIBRARIES} ${Boost_LIBRARIES})

install(TARGETS ${CMAKE_PROJECT_NAME}
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/remote DESTINATION include)

if(UNIX)
    set(CPACK_CMAKE_GENERATOR "Unix Makefiles")
    set(CPACK_SOURCE_GENERATOR "TGZ;TBZ2")
    set(CPACK_GENERATOR "TGZ;TBZ2")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A simple RPC library for C++")
    set(CPACK_PACKAGE_VENDOR "Jan Deinhard")
    set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_PACKAGE_VERSION_MAJOR "0")
    set(CPACK_PACKAGE_VERSION_MINOR "1")
    set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}")
    set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${CPACK_PACKAGE_VERSION}")
    set(CPACK_PACKAGE_CONTACT "Jan Deinhard <jan.deinhard@gmail.com>")
    # set(CPACK_PACKAGE_SECTION "games")
    include(CPack)
endif(UNIX)
