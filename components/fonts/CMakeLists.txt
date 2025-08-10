file(GLOB_RECURSE SOURCES generated/*.c)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS .
    REQUIRES lvgl
)
