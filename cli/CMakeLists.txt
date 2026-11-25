add_executable(humansim main.cpp)
target_link_libraries(humansim PRIVATE hsim)
hsim_set_options(humansim)
