include(Catch)

add_executable(strata-tests 
    test_uuid.cpp 
    test_batch_manager.cpp 
    test_api.cpp
    test_size_parser.cpp
)
target_link_libraries(strata-tests
    PRIVATE
    Catch2::Catch2WithMain
    strata-core
)

catch_discover_tests(strata-tests)
