include(${CMAKE_CURRENT_LIST_DIR}/generated_sources.cmake)

idf_component_register(
    SRCS ${GENERATED_SOURCES}
    INCLUDE_DIRS .
    REQUIRES lvgl
)
