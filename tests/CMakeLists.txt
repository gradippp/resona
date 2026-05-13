include(Catch)

add_executable(resona-tests 
    test_main.cpp
    test_uuid.cpp 
    test_batch_manager.cpp 
    test_api.cpp
    test_size_parser.cpp
    test_media_service.cpp
    test_time_utils.cpp
)
target_link_libraries(resona-tests
    PRIVATE
    Catch2::Catch2
    resona-core
)

catch_discover_tests(resona-tests)
