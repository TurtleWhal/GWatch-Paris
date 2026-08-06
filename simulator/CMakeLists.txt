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
#
# Skip the demo / example / thorvg targets — none of them are wired into the
# watch UI, and dropping them cuts the no-change rebuild from ~five
# "Built target" lines to one. ThorVG provides vector-graphic rendering for
# LVGL's `lv_vg` widgets, which the firmware doesn't use; the matching
# LV_USE_VECTOR_GRAPHIC / LV_USE_THORVG_INTERNAL switches in lv_conf.h are
# off to match.
set(CONFIG_LV_BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS           OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL   OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)
target_include_directories(lvgl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_ROOT}/managed_components/espressif__cjson/cJSON
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# -----------------------------------------------------------------------------
# SDL — native builds find it via pkg-config; the web build pulls
# Emscripten's bundled SDL2 port in via -sUSE_SDL=2.
if(EMSCRIPTEN)
    # Apply to lvgl directly (PUBLIC so anything linking lvgl also gets
    # the include search path emcc materializes). `target_compile_options`
    # is needed because the lvgl target sets its own COMPILE_OPTIONS that
    # override anything `add_compile_options` had pushed earlier.
    target_compile_options(lvgl PUBLIC "SHELL:-sUSE_SDL=2")
    set(SDL2_INCLUDE_DIRS "")
    set(SDL2_LIBRARIES "")
else()
    find_package(SDL2 REQUIRED)
    find_package(Threads REQUIRED)
    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
endif()

# -----------------------------------------------------------------------------
# Sources
file(GLOB UI_SRCS CONFIGURE_DEPENDS
    "${PROJECT_ROOT}/main/ui/*.cpp"
    "${PROJECT_ROOT}/main/ui/faces/*.cpp"
    "${PROJECT_ROOT}/main/ui/screens/*.cpp"
)
file(GLOB FONT_SRCS  CONFIGURE_DEPENDS "${PROJECT_ROOT}/components/fonts/generated/*.c")
file(GLOB IMAGE_SRCS CONFIGURE_DEPENDS "main.cpp" "${PROJECT_ROOT}/components/images/generated/*.c")
file(GLOB ASSET_SRCS CONFIGURE_DEPENDS "${PROJECT_ROOT}/main/assets/*.c")

set(SIM_SRCS
    main.cpp
    hal_sdl.c
    mouse_cursor_icon.c
    src/stubs.cpp
    src/sim_placeholders.cpp
    ${PROJECT_ROOT}/main/system/schedule.cpp
    ${PROJECT_ROOT}/managed_components/espressif__cjson/cJSON/cJSON.c
)
if(EMSCRIPTEN)
    list(APPEND SIM_SRCS src/sim_controls_web.cpp src/sim_tasks_web.cpp)
else()
    list(APPEND SIM_SRCS src/sim_controls.cpp)
endif()

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
    # notifications.cpp does `#include "core/lv_obj_event_private.h"` —
    # path resolves when ${LVGL_ROOT_DIR}/src is on the include path. The
    # IDF component manager adds it on-device; we mirror that here so the
    # firmware sources compile unchanged.
    ${lvgl_SOURCE_DIR}/src
)

target_compile_definitions(gwatch_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    # macOS clang's <math.h> defines M_PI/M_PI_2 but not M_TWOPI; ESP-IDF's
    # toolchain ships it. Patch via the compile line rather than touching
    # the on-device source.
    M_TWOPI=6.283185307179586
)

# Where sim_placeholders.cpp looks for .rgb565 image files. Native build
# reads them off disk via absolute path. Web build sees them at a virtual
# path inside Emscripten's MEMFS — see the --preload-file flag below.
if(EMSCRIPTEN)
    target_compile_definitions(gwatch_sim PRIVATE SIM_PLACEHOLDERS_DIR="/placeholders")
else()
    target_compile_definitions(gwatch_sim PRIVATE SIM_PLACEHOLDERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/placeholders")
endif()

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

if(EMSCRIPTEN)
    target_link_libraries(gwatch_sim PRIVATE lvgl)

    # Emscripten link flags. ccall + cwrap give the HTML shell a way to
    # invoke our exported sim_* functions; EXPORTED_FUNCTIONS pins those
    # symbols so the linker doesn't drop them. Shell file wraps the
    # generated JS in our HTML layout (canvas + control buttons).
    set(SIM_EXPORTS
        "_main,_sim_notify,_sim_dismiss,_sim_clear,_sim_toggle_charging,_sim_battery_step"
    )
    target_link_options(gwatch_sim PRIVATE
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sINITIAL_MEMORY=64MB"
        "SHELL:-sEXPORTED_FUNCTIONS=[${SIM_EXPORTS}]"
        "SHELL:-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap]"
        "SHELL:--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        # Bundle simulator/placeholders/ into the WASM at /placeholders/
        # in Emscripten's MEMFS. fopen("/placeholders/album_art.rgb565")
        # inside sim_placeholders.cpp finds it without network fetches.
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/placeholders@/placeholders"
    )
    set_target_properties(gwatch_sim PROPERTIES SUFFIX ".html")
    # Make CMake re-link when the shell template or any file in placeholders/
    # changes. emcc's --shell-file / --preload-file are read at link time
    # but not tracked as deps automatically, so without LINK_DEPENDS a
    # shell.html edit gets baked in only after a clean build.
    file(GLOB SIM_PRELOAD_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/placeholders/*")
    set_target_properties(gwatch_sim PROPERTIES
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html;${SIM_PRELOAD_FILES}"
    )

    # Wrapper page: index.html is the "polished" entry point that just
    # iframes the bare gwatch_sim.html. Kept side-by-side in build-web/
    # so previewing locally serves both, and copying build-web/* into a
    # static host gives the user / and /gwatch_sim.html out of the box.
    # configure_file with COPYONLY tracks the source file as a dep — edit
    # web/index.html and the next build copies it through.
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
        ${CMAKE_BINARY_DIR}/index.html
        COPYONLY
    )
else()
    target_link_libraries(gwatch_sim PRIVATE
        lvgl
        ${SDL2_LIBRARIES}
        Threads::Threads
        m
    )

    # `run` convenience target.
    add_custom_target(run COMMAND $<TARGET_FILE:gwatch_sim> DEPENDS gwatch_sim)
endif()
