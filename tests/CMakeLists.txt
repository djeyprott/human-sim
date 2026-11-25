FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    GIT_SHALLOW    TRUE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

add_executable(hsim_tests
    main.cpp
    test_ids.cpp
    test_calendar.cpp
    test_rng.cpp
    test_jobs.cpp
    test_event_log.cpp
    test_snapshots.cpp
    test_world.cpp
)
target_link_libraries(hsim_tests PRIVATE hsim doctest::doctest)
hsim_set_options(hsim_tests)
add_test(NAME hsim_tests COMMAND hsim_tests)
