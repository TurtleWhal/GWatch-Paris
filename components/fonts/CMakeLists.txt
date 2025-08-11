include(${CMAKE_CURRENT_LIST_DIR}/generated_sources.cmake)

idf_component_register(
    SRCS ${GENERATED_SOURCES}
    INCLUDE_DIRS .
    REQUIRES lvgl
)

# add_custom_command(
#     OUTPUT ${CMAKE_BINARY_DIR}/script_done.marker
#     COMMAND ${CMAKE_COMMAND} -E echo "Running script..."
#     COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=$ENV{PYTHONPATH} python3 ${CMAKE_CURRENT_LIST_DIR}/fonts.py
#     COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_BINARY_DIR}/script_done.marker
#     COMMENT "Running pre-build script"
# )

# add_custom_target(run_prebuild_script ALL
#     DEPENDS ${CMAKE_BINARY_DIR}/script_done.marker
# )

# add_dependencies(app run_prebuild_script)

# add_custom_target(before_flash
#     COMMAND ${PYTHON} ${CMAKE_CURRENT_LIST_DIR}/fonts.py
# )
# add_dependencies(app before_flash)

# add_custom_target(before_flash
# 	COMMAND ${CMAKE_COMMAND} -E echo "before_flash"
# )
# add_dependencies(app before_flash)
# add_custom_command(POST_BUILD
# 	COMMAND ${CMAKE_COMMAND} -E echo "after_flash"
# )