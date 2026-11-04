find_package(Qt6 REQUIRED COMPONENTS Test)

# qtranslate_add_test(<name>) builds tests/<name>.cpp against qtranslate_core
# and registers it with CTest. Saved service responses are in tests/fixtures,
# available to the test as QTRANSLATE_FIXTURES_DIR.
function(qtranslate_add_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE qtranslate_core Qt::Test)
    target_compile_definitions(${name} PRIVATE
        QTRANSLATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
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
qtranslate_add_test(tst_googleparser)
qtranslate_add_test(tst_googletranslateservice)
qtranslate_add_test(tst_hotkeysequence)
qtranslate_add_test(tst_languages)
qtranslate_add_test(tst_textchunker)
qtranslate_add_test(tst_translationmanager)

if(WIN32)
    qtranslate_add_test(tst_hotkeymanager
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager.h
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager_win.cpp
    )
    target_link_libraries(tst_hotkeymanager PRIVATE user32)
endif()
