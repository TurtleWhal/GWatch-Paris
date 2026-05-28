cmake_minimum_required(VERSION 3.12)
project(gwatch_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The watch project's UI uses C++17 lambdas (LVGL event callbacks) and a
# bunch of `lv_obj_t * = ...` lines that older standards reject.

# -----------------------------------------------------------------------------
# Where the upstream LVGL simulator template lives. The default points at the
# user's existing clone; override on the command line if it moved:
#   cmake -B build -DLV_PORT_DIR=/path/to/lv_port_pc_vscode
# We use that repo for two things only: its bundled lvgl/ submodule and its
# (already-tuned-for-SDL) lv_conf.h. The watch project's own lv_conf.h
# lives next to this CMakeLists.txt and is the one actually consumed via
# LV_CONF_INCLUDE_SIMPLE.
set(LV_PORT_DIR "/Users/garrett/Documents/github/lv_port_pc_vscode"
    CACHE PATH "Path to the lv_port_pc_vscode checkout (provides lvgl/)")

if(NOT EXISTS "${LV_PORT_DIR}/lvgl/CMakeLists.txt")
    message(FATAL_ERROR
        "LVGL not found at ${LV_PORT_DIR}/lvgl. "
        "Clone https://github.com/lvgl/lv_port_pc_vscode --recursive "
        "or pass -DLV_PORT_DIR=<path> on the cmake command line.")
endif()

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# -----------------------------------------------------------------------------
# Dependencies
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# -----------------------------------------------------------------------------
# LVGL — pulled from the simulator template's submodule. We hand its target
# the simulator dir on the include path so its `#include "lv_conf.h"` lands
# on ours (next to this CMakeLists.txt), not the template's.
add_subdirectory(${LV_PORT_DIR}/lvgl ${CMAKE_BINARY_DIR}/lvgl_build)
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# -----------------------------------------------------------------------------
# Sources
file(GLOB UI_SRCS
    "${PROJECT_ROOT}/main/ui/*.cpp"
    "${PROJECT_ROOT}/main/ui/faces/*.cpp"
    "${PROJECT_ROOT}/main/ui/screens/*.cpp"
)

file(GLOB FONT_SRCS "${PROJECT_ROOT}/components/fonts/generated/*.c")
file(GLOB IMAGE_SRCS "${PROJECT_ROOT}/components/images/generated/*.c")
file(GLOB ASSET_SRCS "${PROJECT_ROOT}/main/assets/*.c")

set(SIM_SRCS
    main.cpp
    hal_sdl.c
    mouse_cursor_icon.c
    src/stubs.cpp
    ${PROJECT_ROOT}/main/system/schedule.cpp
)

add_executable(gwatch_sim
    ${SIM_SRCS}
    ${UI_SRCS}
    ${FONT_SRCS}
    ${IMAGE_SRCS}
    ${ASSET_SRCS}
)

target_include_directories(gwatch_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shim/include
    ${PROJECT_ROOT}/main
    ${PROJECT_ROOT}/main/system
    ${PROJECT_ROOT}/main/hardware
    ${PROJECT_ROOT}/main/display
    ${PROJECT_ROOT}/main/ui
    ${PROJECT_ROOT}/components/fonts
    ${PROJECT_ROOT}/components/images
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(gwatch_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    # macOS clang's <math.h> defines M_PI/M_PI_2 but not M_TWOPI; ESP-IDF's
    # toolchain ships it. Patch via the compile line rather than touching
    # the on-device source.
    M_TWOPI=6.283185307179586
    # The watch's UI uses LVGL 9.5's v8-compat rotation names
    # (LV_DISP_ROTATION_*). The simulator's 9.6-dev v8 map dropped those
    # specific aliases — restore them here.
    LV_DISP_ROTATION_0=LV_DISPLAY_ROTATION_0
    LV_DISP_ROTATION_90=LV_DISPLAY_ROTATION_90
    LV_DISP_ROTATION_180=LV_DISPLAY_ROTATION_180
    LV_DISP_ROTATION_270=LV_DISPLAY_ROTATION_270
)

# Watch UI was written for ESP-IDF's compilers, which are looser about a
# couple of things than vanilla clang/gcc on the host:
target_compile_options(gwatch_sim PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-format
)

target_link_libraries(gwatch_sim PRIVATE
    lvgl
    lvgl::thorvg
    ${SDL2_LIBRARIES}
    Threads::Threads
    m
)

# `run` convenience target (matches the upstream simulator's UX).
add_custom_target(run COMMAND $<TARGET_FILE:gwatch_sim> DEPENDS gwatch_sim)
