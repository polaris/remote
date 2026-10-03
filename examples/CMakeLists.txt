add_executable(kv_server kv_store/kv_server.cpp)
target_link_libraries(kv_server PRIVATE remote::remote remote_project_options)

add_executable(kv_client kv_store/kv_client.cpp)
target_link_libraries(kv_client PRIVATE remote::remote remote_project_options)
