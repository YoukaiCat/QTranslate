find_package(Qt6 REQUIRED COMPONENTS Test)

# qtranslate_add_test(<name>) builds tests/<name>.cpp against qtranslate_core
# and registers it with CTest.
function(qtranslate_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE qtranslate_core Qt::Test)
    qtranslate_set_compile_options(${name})
    add_test(NAME ${name} COMMAND ${name})

    # Let tests find the Qt DLLs without windeployqt.
    if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endfunction()

qtranslate_add_test(tst_appinfo)
