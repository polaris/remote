add_executable(kv_server kv_store/kv_server.cpp)
target_link_libraries(kv_server PRIVATE remote::remote remote_project_options)

add_executable(kv_client kv_store/kv_client.cpp)
target_link_libraries(kv_client PRIVATE remote::remote remote_project_options)

if(REMOTE_BUILD_TESTS)
    include(Catch)

    add_executable(kv_store_tests
            kv_store/test_kv_store.cpp)
    target_link_libraries(kv_store_tests PRIVATE remote::remote Catch2::Catch2WithMain remote_project_options)

    catch_discover_tests(kv_store_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 60)
endif()