cmake_minimum_required(VERSION 3.24)

project(remote
        VERSION 0.2.0
        DESCRIPTION "Typed msgpack-rpc for C++20, built on Boost.Asio coroutines"
        HOMEPAGE_URL "https://github.com/polaris/remote"
        LANGUAGES CXX)

option(REMOTE_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(REMOTE_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
set(REMOTE_SANITIZERS "" CACHE STRING "Sanitizers to build with, for example address,undefined or thread")
option(REMOTE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(REMOTE_CLANG_TIDY "Run clang-tidy on this project's sources while building" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Boost 1.85 REQUIRED CONFIG)
find_package(msgpack-cxx REQUIRED CONFIG)
find_package(Threads REQUIRED)

# Build settings for the targets of this project; not propagated to consumers.
add_library(remote_project_options INTERFACE)
target_compile_definitions(remote_project_options INTERFACE BOOST_ASIO_NO_DEPRECATED)
if(MSVC)
    target_compile_options(remote_project_options INTERFACE /W4 $<$<BOOL:${REMOTE_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(remote_project_options INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            $<$<BOOL:${REMOTE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()
if(REMOTE_SANITIZERS)
    target_compile_options(remote_project_options INTERFACE -fsanitize=${REMOTE_SANITIZERS} -fno-omit-frame-pointer)
    target_link_options(remote_project_options INTERFACE -fsanitize=${REMOTE_SANITIZERS})
endif()

# Set before any target is created, so that it applies to the library, tests and examples.
if(REMOTE_CLANG_TIDY)
    # Prefer the version pinned in requirements.txt, installed into .venv.
    find_program(REMOTE_CLANG_TIDY_EXE NAMES clang-tidy HINTS ${PROJECT_SOURCE_DIR}/.venv/bin REQUIRED)
    set(CMAKE_CXX_CLANG_TIDY ${REMOTE_CLANG_TIDY_EXE})
    if(REMOTE_WARNINGS_AS_ERRORS)
        list(APPEND CMAKE_CXX_CLANG_TIDY --warnings-as-errors=*)
    endif()
endif()

add_library(remote
        src/client.cpp
        src/connection.cpp
        src/error.cpp
        src/protocol.cpp
        src/server.cpp)
add_library(remote::remote ALIAS remote)
target_include_directories(remote PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(remote PUBLIC cxx_std_20)
target_link_libraries(remote
        PUBLIC Boost::headers msgpack-cxx Threads::Threads
        PRIVATE $<BUILD_INTERFACE:remote_project_options>)
set_target_properties(remote PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(REMOTE_BUILD_TESTS)
    find_package(Catch2 3 REQUIRED CONFIG)
    enable_testing()
    add_subdirectory(tests)
endif()

if(REMOTE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS remote EXPORT remoteTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT remoteTargets
        NAMESPACE remote::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/remote)
configure_package_config_file(cmake/remoteConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/remoteConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/remote)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/remoteConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/remoteConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/remoteConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/remote)
