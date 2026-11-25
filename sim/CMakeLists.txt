add_library(hsim STATIC
    src/binary_io.cpp
    src/calendar.cpp
    src/catalog.cpp
    src/event_log.cpp
    src/jobs.cpp
    src/snapshots.cpp
    src/world.cpp
    src/systems/wanderers.cpp
)
target_include_directories(hsim PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(hsim PUBLIC Threads::Threads)
hsim_set_options(hsim)
