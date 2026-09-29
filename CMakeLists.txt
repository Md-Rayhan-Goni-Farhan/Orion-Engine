cmake_minimum_required(VERSION 3.25)
project(OrionEngine VERSION 0.0.11 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")

set(DEPS_DIR ${CMAKE_SOURCE_DIR}/deps)

set(SDL3_DIR ${DEPS_DIR}/SDL3-3.4.10/cmake)
find_package(SDL3 REQUIRED CONFIG)

set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/bgfx.cmake)

set(BUILD_SHARED_LIBS OFF)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${DEPS_DIR}/JoltPhysics/Build)

file(GLOB LUA_SOURCES ${DEPS_DIR}/lua-5.4.7/src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${DEPS_DIR}/lua-5.4.7/src/lua.c)
list(REMOVE_ITEM LUA_SOURCES ${DEPS_DIR}/lua-5.4.7/src/luac.c)
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${DEPS_DIR}/lua-5.4.7/src)

add_library(imgui STATIC
    ${DEPS_DIR}/imgui/imgui.cpp
    ${DEPS_DIR}/imgui/imgui_draw.cpp
    ${DEPS_DIR}/imgui/imgui_tables.cpp
    ${DEPS_DIR}/imgui/imgui_widgets.cpp
    ${DEPS_DIR}/imgui/imgui_demo.cpp
    ${DEPS_DIR}/imgui/backends/imgui_impl_sdl3.cpp
)
target_include_directories(imgui PUBLIC
    ${DEPS_DIR}/imgui
    ${DEPS_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_subdirectory(${DEPS_DIR}/meshoptimizer)

add_executable(OrionEngine
    engine/src/core/main.cpp
    engine/src/editor/imgui_bgfx.cpp
    engine/src/renderer/mesh_renderer.cpp
    engine/src/renderer/grid_renderer.cpp
    engine/src/renderer/light_renderer.cpp
    deps/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(OrionEngine PRIVATE
    engine/include
    engine/src/editor
    engine/src/renderer
    engine/src/world
    ${DEPS_DIR}/sol2/include
    ${DEPS_DIR}/bgfx.cmake/bgfx/include
    ${DEPS_DIR}/bgfx.cmake/bx/include
    ${DEPS_DIR}/bgfx.cmake/bimg/include
    ${DEPS_DIR}
    ${DEPS_DIR}/ImGuizmo
)

target_compile_options(OrionEngine PRIVATE /Zc:__cplusplus /Zc:preprocessor)

target_link_libraries(OrionEngine PRIVATE
    SDL3::SDL3 bgfx Jolt lua imgui meshoptimizer comdlg32
)