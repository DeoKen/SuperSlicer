get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests.cpp
    ../../src/plugins_cpp/Polyholes/Polyholes.cpp
    plugin_test_helpers.cpp
    test_polyholes.cpp
    test_support_demand_modifiers.cpp
    test_support_demand_overhangs.cpp
    )

target_link_libraries(${_TEST_NAME}_tests test_common test_common_data libslic3r)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    prusaslicer_copy_dlls(${_TEST_NAME}_tests)
endif()

if (TARGET python_plugin_loader)
    add_dependencies(${_TEST_NAME}_tests python_plugin_loader)
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE SLIC3R_TEST_PYTHON_PLUGINS)
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:python_plugin_loader>"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/plugins/$<TARGET_FILE_NAME:python_plugin_loader>"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "$<TARGET_FILE_DIR:python_plugin_loader>/python"
            "$<TARGET_FILE_DIR:${_TEST_NAME}_tests>/plugins/python"
        COMMENT "Copy Python plugin runtime to plugin tests"
        VERBATIM
    )
endif()

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "[plugins]" ${CATCH_EXTRA_ARGS})
