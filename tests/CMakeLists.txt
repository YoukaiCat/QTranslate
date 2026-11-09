find_package(Qt6 REQUIRED COMPONENTS Test)

# qtranslate_add_test(<name>) builds tests/<name>.cpp against qtranslate_core
# and registers it with CTest. Saved service responses are in tests/fixtures,
# available to the test as QTRANSLATE_FIXTURES_DIR.
function(qtranslate_add_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE qtranslate_core Qt::Test)
    target_compile_definitions(${name} PRIVATE
        QTRANSLATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        QTRANSLATE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
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
qtranslate_add_test(tst_bingwebparser)
qtranslate_add_test(tst_bingwebservice)
qtranslate_add_test(tst_deeplparser)
qtranslate_add_test(tst_deeplservice)
qtranslate_add_test(tst_deeplwebparser)
qtranslate_add_test(tst_deeplwebservice)
qtranslate_add_test(tst_googleparser)
qtranslate_add_test(tst_googletranslateservice)
qtranslate_add_test(tst_hotkeysequence)
qtranslate_add_test(tst_languages)
qtranslate_add_test(tst_languagetoolparser)
qtranslate_add_test(tst_languagetoolservice)
qtranslate_add_test(tst_mozhiparser)
qtranslate_add_test(tst_mozhiwebservice)
qtranslate_add_test(tst_mymemoryparser)
qtranslate_add_test(tst_mymemoryservice)
qtranslate_add_test(tst_networkmanager)
qtranslate_add_test(tst_openaichat)
qtranslate_add_test(tst_openaicompatibleservice)
qtranslate_add_test(tst_popupplacement)
qtranslate_add_test(tst_popupwindow
    ${PROJECT_SOURCE_DIR}/src/ui/popupwindow.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/popupwindow.h
    ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.h
)
target_link_libraries(tst_popupwindow PRIVATE Qt::Widgets)
set_tests_properties(tst_popupwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
qtranslate_add_test(tst_proofreadingmanager)
qtranslate_add_test(tst_proofreadingpanel
    ${PROJECT_SOURCE_DIR}/src/ui/mainwindow.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/mainwindow.h
    ${PROJECT_SOURCE_DIR}/src/ui/proofreadingpanel.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/proofreadingpanel.h
    ${PROJECT_SOURCE_DIR}/src/ui/proofreadingerrortext.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/proofreadingerrortext.h
    ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.h
    ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/translationerrortext.h
)
target_link_libraries(tst_proofreadingpanel PRIVATE Qt::Widgets)
set_tests_properties(tst_proofreadingpanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
qtranslate_add_test(tst_queuedwebservice)
qtranslate_add_test(tst_reversoparser)
qtranslate_add_test(tst_reversoservice)
qtranslate_add_test(tst_settings)
qtranslate_add_test(tst_settingsdialog
    ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/languagecombobox.h
    ${PROJECT_SOURCE_DIR}/src/ui/settingsdialog.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/settingsdialog.h
)
target_link_libraries(tst_settingsdialog PRIVATE Qt::Widgets)
set_tests_properties(tst_settingsdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
qtranslate_add_test(tst_textchunker)
qtranslate_add_test(tst_translationmanager)
qtranslate_add_test(tst_translations)
qtranslate_add_test(tst_uilanguages)

if(WIN32)
    qtranslate_add_test(tst_hotkeymanager
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager.h
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager_win.cpp
    )
    target_link_libraries(tst_hotkeymanager PRIVATE user32)

    qtranslate_add_test(tst_autostart
        ${PROJECT_SOURCE_DIR}/src/platform/autostart.h
        ${PROJECT_SOURCE_DIR}/src/platform/autostart_win.cpp
    )

    qtranslate_add_test(tst_secretprotector
        ${PROJECT_SOURCE_DIR}/src/platform/secretprotector.h
        ${PROJECT_SOURCE_DIR}/src/platform/secretprotector_win.cpp
    )
    target_link_libraries(tst_secretprotector PRIVATE crypt32)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qtranslate_add_test(tst_autostart
        ${PROJECT_SOURCE_DIR}/src/platform/autostart.h
        ${PROJECT_SOURCE_DIR}/src/platform/autostart_linux.cpp
    )

    qtranslate_add_test(tst_secretprotector
        ${PROJECT_SOURCE_DIR}/src/platform/secretprotector.h
        ${PROJECT_SOURCE_DIR}/src/platform/secretprotector_linux.cpp
    )
    target_link_libraries(tst_secretprotector PRIVATE Qt6::DBus)
    if(LIBSECRET_FOUND)
        target_link_libraries(tst_secretprotector PRIVATE PkgConfig::LIBSECRET)
        target_compile_definitions(tst_secretprotector PRIVATE QTRANSLATE_HAVE_LIBSECRET)
    endif()

    # The Linux hotkey tests compile the same translation unit (X11 plus the
    # GlobalShortcuts portal backend), so they link X11 and QtDBus.
    qtranslate_add_test(tst_hotkeymanager_x11
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager.h
        ${PROJECT_SOURCE_DIR}/src/platform/portalshortcut.h
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager_linux.cpp
    )
    target_link_libraries(tst_hotkeymanager_x11 PRIVATE X11::X11 Qt6::DBus)
    if(X11_XTest_FOUND)
        target_link_libraries(tst_hotkeymanager_x11 PRIVATE X11::Xtst)
        target_compile_definitions(tst_hotkeymanager_x11 PRIVATE QTRANSLATE_HAVE_XTEST)
    endif()

    qtranslate_add_test(tst_hotkeymanager_wayland
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager.h
        ${PROJECT_SOURCE_DIR}/src/platform/portalshortcut.h
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager_linux.cpp
    )
    # The X11 symbols are linked because the translation unit contains the X11
    # backend, but the Wayland test never opens a display.
    target_link_libraries(tst_hotkeymanager_wayland PRIVATE X11::X11 Qt6::DBus)

    # The GUI tests need the xcb platform plugin even when CI exports
    # QT_QPA_PLATFORM=offscreen; they run under xvfb-run.
    qtranslate_add_test(tst_selectiongrabber_linux
        ${PROJECT_SOURCE_DIR}/src/platform/selectiongrabber.h
        ${PROJECT_SOURCE_DIR}/src/platform/sessiontype.h
        ${PROJECT_SOURCE_DIR}/src/platform/selectiongrabber_linux.cpp
    )
    target_link_libraries(tst_selectiongrabber_linux PRIVATE Qt::Widgets X11::X11 X11::Xtst)
    set_tests_properties(tst_selectiongrabber_linux PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=xcb")

    # The integration test wires the real HotkeyManager to the real
    # SelectionGrabber, so it compiles both Linux backends. QtDBus is linked
    # because the hotkey translation unit contains the portal backend.
    qtranslate_add_test(tst_hotkey_selection_x11
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager.h
        ${PROJECT_SOURCE_DIR}/src/platform/portalshortcut.h
        ${PROJECT_SOURCE_DIR}/src/platform/hotkeymanager_linux.cpp
        ${PROJECT_SOURCE_DIR}/src/platform/selectiongrabber.h
        ${PROJECT_SOURCE_DIR}/src/platform/sessiontype.h
        ${PROJECT_SOURCE_DIR}/src/platform/selectiongrabber_linux.cpp
    )
    target_link_libraries(tst_hotkey_selection_x11 PRIVATE
        Qt::Widgets X11::X11 X11::Xtst Qt6::DBus)
    set_tests_properties(tst_hotkey_selection_x11 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=xcb")

    qtranslate_add_test(tst_windowactivation_linux
        ${PROJECT_SOURCE_DIR}/src/platform/windowactivation.h
        ${PROJECT_SOURCE_DIR}/src/platform/sessiontype.h
        ${PROJECT_SOURCE_DIR}/src/platform/windowactivation_linux.cpp
    )
    target_link_libraries(tst_windowactivation_linux PRIVATE Qt::Widgets X11::X11 X11::Xtst)
    set_tests_properties(tst_windowactivation_linux PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=xcb")

    # The CLI test is guiless: it parses the shared options and drives the
    # real single-instance socket with a child process, so it needs no display.
    qtranslate_add_test(tst_cli
        ${PROJECT_SOURCE_DIR}/src/app/commandline.cpp
        ${PROJECT_SOURCE_DIR}/src/app/commandline.h
        ${PROJECT_SOURCE_DIR}/src/app/singleinstance.cpp
        ${PROJECT_SOURCE_DIR}/src/app/singleinstance.h
        ${PROJECT_SOURCE_DIR}/src/app/logging.cpp
        ${PROJECT_SOURCE_DIR}/src/app/logging.h
        ${PROJECT_SOURCE_DIR}/src/platform/sessiontype.h
        ${PROJECT_SOURCE_DIR}/src/platform/windowactivation.h
        ${PROJECT_SOURCE_DIR}/src/platform/windowactivation_linux.cpp
    )
    target_link_libraries(tst_cli PRIVATE X11::X11 X11::Xtst)
endif()

# Platform-neutral: the Yandex web service talks to a local QTcpServer only.
qtranslate_add_test(tst_yandexwebparser)
qtranslate_add_test(tst_yandexwebservice)
