cmake_minimum_required(VERSION 3.24)
project(human_sim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HSIM_BUILD_APP   "Build the Dear ImGui viewer (the window itself is Windows only)" ON)
option(HSIM_BUILD_TESTS "Build the test suite" ON)

include(cmake/Warnings.cmake)
include(FetchContent)

add_subdirectory(sim)
add_subdirectory(cli)

if(HSIM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HSIM_BUILD_APP)
    add_subdirectory(app)
endif()
