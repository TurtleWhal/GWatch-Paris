cmake_minimum_required(VERSION 3.14)
project(gwatch_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The watch project's UI uses C++17 lambdas (LVGL event callbacks) and
# `lv_obj_t *foo = ...` lines that older standards reject.

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# -----------------------------------------------------------------------------
# LVGL — fetched from upstream at the same tag the firmware pins via the IDF
# component registry (lvgl/lvgl: ^9.4.0 → v9.5.0). This keeps the simulator
# behaviour identical to on-device. The fetch happens once at configure time;
# rebuilds are free.
include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# -----------------------------------------------------------------------------
# SDL
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})

# -----------------------------------------------------------------------------
# Sources
file(GLOB UI_SRCS
    "${PROJECT_ROOT}/main/ui/*.cpp"
    "${PROJECT_ROOT}/main/ui/faces/*.cpp"
    "${PROJECT_ROOT}/main/ui/screens/*.cpp"
)
file(GLOB FONT_SRCS  "${PROJECT_ROOT}/components/fonts/generated/*.c")
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
)

# The watch UI was written for ESP-IDF's compilers, which are looser about
# a couple of things than vanilla clang/gcc on the host:
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

# `run` convenience target.
add_custom_target(run COMMAND $<TARGET_FILE:gwatch_sim> DEPENDS gwatch_sim)
