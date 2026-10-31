cmake_minimum_required(VERSION 3.19)
project(qtranslate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

qt_add_executable(qtranslate
    WIN32
    src/main.cpp
    src/appcontroller.cpp   src/appcontroller.h
    src/appicon.cpp         src/appicon.h
    src/autostart.cpp       src/autostart.h
    src/hotkeymanager.cpp   src/hotkeymanager.h
    src/languages.cpp       src/languages.h
    src/mainwindow.cpp      src/mainwindow.h
    src/popupwindow.cpp     src/popupwindow.h
    src/selectiongrabber.cpp src/selectiongrabber.h
    src/settings.cpp        src/settings.h
    src/settingsdialog.cpp  src/settingsdialog.h
    src/translator.cpp      src/translator.h
)

if(MSVC)
    target_compile_options(qtranslate PRIVATE /utf-8 /W4)
endif()

target_link_libraries(qtranslate
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        user32
)

install(TARGETS qtranslate
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET qtranslate
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
