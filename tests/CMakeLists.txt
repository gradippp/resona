include(Catch)

add_executable(cloud-dl-tests
    test_uuid.cpp
    test_batch_manager.cpp
    test_api.cpp
)

target_link_libraries(cloud-dl-tests
    PRIVATE
    Catch2::Catch2WithMain
    cloud-dl-core
)

catch_discover_tests(cloud-dl-tests)
