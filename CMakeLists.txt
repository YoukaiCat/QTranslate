cmake_minimum_required(VERSION 3.19)
project(qtranslate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

option(QTRANSLATE_BUILD_TESTS "Build unit tests" ON)

function(qtranslate_set_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive-)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# qtranslate_core: UI- and WinAPI-free logic shared by the app and the tests.
# ---------------------------------------------------------------------------
qt_add_library(qtranslate_core STATIC
    src/core/appinfo.cpp                    src/core/appinfo.h
    src/core/hotkeysequence.cpp             src/core/hotkeysequence.h
    src/core/languages.cpp                  src/core/languages.h
    src/core/textchunker.cpp                src/core/textchunker.h
    src/core/translationmanager.cpp         src/core/translationmanager.h
    src/core/translationservice.cpp         src/core/translationservice.h
    src/core/translationtypes.h
    src/net/networkmanager.cpp              src/net/networkmanager.h
    src/services/googleparser.cpp           src/services/googleparser.h
    src/services/googletranslateservice.cpp src/services/googletranslateservice.h
)

target_include_directories(qtranslate_core PUBLIC src)
target_link_libraries(qtranslate_core PUBLIC Qt::Core Qt::Gui Qt::Network)
set_source_files_properties(src/core/appinfo.cpp PROPERTIES
    COMPILE_DEFINITIONS QTRANSLATE_VERSION="${PROJECT_VERSION}"
)
qtranslate_set_compile_options(qtranslate_core)

# ---------------------------------------------------------------------------
# qtranslate: the desktop application.
# ---------------------------------------------------------------------------
qt_add_executable(qtranslate
    WIN32
    src/main.cpp
    src/app/appcontroller.cpp       src/app/appcontroller.h
    src/app/logging.cpp             src/app/logging.h
    src/app/singleinstance.cpp      src/app/singleinstance.h
    src/platform/hotkeymanager.h
    src/platform/selectiongrabber.h
    src/platform/windowactivation.h
    src/ui/appicon.cpp              src/ui/appicon.h
    src/ui/languagecombobox.cpp     src/ui/languagecombobox.h
    src/ui/mainwindow.cpp           src/ui/mainwindow.h
    src/ui/translationerrortext.cpp src/ui/translationerrortext.h
    src/ui/trayicon.cpp             src/ui/trayicon.h
)

if(WIN32)
    target_sources(qtranslate PRIVATE
        src/platform/hotkeymanager_win.cpp
        src/platform/selectiongrabber_win.cpp
        src/platform/windowactivation_win.cpp
    )
else()
    target_sources(qtranslate PRIVATE
        src/platform/hotkeymanager_stub.cpp
        src/platform/selectiongrabber_stub.cpp
        src/platform/windowactivation_stub.cpp
    )
endif()

target_link_libraries(qtranslate
    PRIVATE
        qtranslate_core
        Qt::Widgets
)

if(WIN32)
    target_link_libraries(qtranslate PRIVATE user32)
endif()

qtranslate_set_compile_options(qtranslate)

install(TARGETS qtranslate
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET qtranslate
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(QTRANSLATE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
