FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b-docking
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

if(WIN32)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 d3dcompiler dxgi dwmapi imm32)

add_executable(humansim_viewer WIN32
    main_win32.cpp
    viewer.cpp
)
target_link_libraries(humansim_viewer PRIVATE hsim imgui comdlg32)
hsim_set_options(humansim_viewer)
endif()

# Headless smoke test: the viewer's UI driven with no window or GPU.
if(HSIM_BUILD_TESTS)
    add_executable(humansim_viewer_smoke
        viewer_smoke.cpp
        viewer.cpp
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    )
    target_include_directories(humansim_viewer_smoke PRIVATE ${imgui_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(humansim_viewer_smoke PRIVATE IMGUI_USER_CONFIG="imconfig_smoke.h")
    target_link_libraries(humansim_viewer_smoke PRIVATE hsim)
    hsim_set_options(humansim_viewer_smoke)
    add_test(NAME viewer_smoke COMMAND humansim_viewer_smoke)
endif()
