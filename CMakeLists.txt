cmake_minimum_required(VERSION 3.20)
project(Syn_Dynamic_Res_Scale CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/EHa)
endif()

include(FetchContent)

# Fetch fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)

# Fetch GLFW for GUI Windowing
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)

# Fetch Dear ImGui for Rich Graphical User Interface
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.5
)

# Fetch OpenVR SDK headers & libs
FetchContent_Declare(
    openvr
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG        v1.23.7
)

FetchContent_MakeAvailable(fmt glfw imgui)
FetchContent_Populate(openvr)

# ImGui sources for OpenGL3 + GLFW
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${glfw_SOURCE_DIR}/include
    ${openvr_SOURCE_DIR}/headers
)

# Executable target: Syn_Dynamic_Res_Scale
add_executable(Syn_Dynamic_Res_Scale WIN32
    src/main_gui.cpp
    ${IMGUI_SOURCES}
)

target_link_libraries(Syn_Dynamic_Res_Scale PRIVATE
    fmt::fmt
    glfw
    opengl32
    ${openvr_SOURCE_DIR}/lib/win64/openvr_api.lib
)

set_target_properties(Syn_Dynamic_Res_Scale PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Release"
)

# Unit tests for the scaling logic (header-only, no VR or GUI dependencies)
enable_testing()
add_executable(smoothing_engine_tests tests/smoothing_engine_tests.cpp)
add_test(NAME smoothing_engine_tests COMMAND smoothing_engine_tests)
add_executable(frame_timing_tests tests/frame_timing_tests.cpp)
add_test(NAME frame_timing_tests COMMAND frame_timing_tests)
