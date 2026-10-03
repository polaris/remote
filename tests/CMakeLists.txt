find_package(Catch2 3 REQUIRED CONFIG)
include(Catch)

add_executable(remote_tests
        test_client_server.cpp
        test_protocol.cpp)
target_link_libraries(remote_tests PRIVATE remote::remote Catch2::Catch2WithMain remote_project_options)

catch_discover_tests(remote_tests DISCOVERY_MODE PRE_TEST)
