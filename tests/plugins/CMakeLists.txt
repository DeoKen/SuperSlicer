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

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "[plugins]" ${CATCH_EXTRA_ARGS})
