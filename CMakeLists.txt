# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# idf_build_set_property(MINIMAL_BUILD ON)

add_compile_definitions(${COMPONENT_LIB} PRIVATE
    LV_COLOR_16_SWAP=0
    LV_LVGL_H_INCLUDE_SIMPLE=1
)

add_definitions(
    -DARDUINO_USB_MODE
    -DARDUINO_USB_CDC_ON_BOOT
)

add_compile_definitions(ENV_WAVESHARE)
# add_compile_definitions(ENV_ SOMETHING ELSE?)

project(GWatch-Paris)
