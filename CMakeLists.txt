cmake_minimum_required(VERSION 3.19)
project(qtranslate VERSION 0.9.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network LinguistTools)

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
    src/core/popupplacement.cpp             src/core/popupplacement.h
    src/core/settings.cpp                   src/core/settings.h
    src/core/textchunker.cpp                src/core/textchunker.h
    src/core/translationmanager.cpp         src/core/translationmanager.h
    src/core/translationservice.cpp         src/core/translationservice.h
    src/core/translationtypes.h
    src/net/networkmanager.cpp              src/net/networkmanager.h
    src/services/deeplparser.cpp            src/services/deeplparser.h
    src/services/deeplservice.cpp           src/services/deeplservice.h
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
    src/platform/autostart.h
    src/platform/hotkeymanager.h
    src/platform/secretprotector.h
    src/platform/selectiongrabber.h
    src/platform/windowactivation.h
    src/ui/appicon.cpp              src/ui/appicon.h
    src/ui/languagecombobox.cpp     src/ui/languagecombobox.h
    src/ui/mainwindow.cpp           src/ui/mainwindow.h
    src/ui/popupwindow.cpp          src/ui/popupwindow.h
    src/ui/settingsdialog.cpp       src/ui/settingsdialog.h
    src/ui/translationerrortext.cpp src/ui/translationerrortext.h
    src/ui/trayicon.cpp             src/ui/trayicon.h
)

if(WIN32)
    target_sources(qtranslate PRIVATE
        resources/qtranslate.rc
        src/platform/autostart_win.cpp
        src/platform/hotkeymanager_win.cpp
        src/platform/secretprotector_win.cpp
        src/platform/selectiongrabber_win.cpp
        src/platform/windowactivation_win.cpp
    )
else()
    target_sources(qtranslate PRIVATE
        src/platform/autostart_stub.cpp
        src/platform/hotkeymanager_stub.cpp
        src/platform/secretprotector_stub.cpp
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
    target_link_libraries(qtranslate PRIVATE user32 crypt32)
endif()

if(MSVC)
    set_property(TARGET qtranslate PROPERTY VS_DPI_AWARE "PerMonitor")
endif()

qtranslate_set_compile_options(qtranslate)

qt_add_translations(qtranslate
    TS_FILES i18n/qtranslate_ru_RU.ts
    RESOURCE_PREFIX "/i18n"
)

install(TARGETS qtranslate
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET qtranslate
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    # The installer bundles the MSVC CRT explicitly below. windeployqt would
    # otherwise add the 18 MB vc_redist.x64.exe when VCINSTALLDIR is set.
    NO_COMPILER_RUNTIME
    # qtranslate uses Qt Widgets, not Direct3D 12; skip the D3D/DXC compilers.
    DEPLOY_TOOL_OPTIONS
        --no-system-d3d-compiler
        --no-system-dxc-compiler
)
install(SCRIPT ${deploy_script})

# windeployqt cannot find the compiler runtime when VCINSTALLDIR is not set
# (e.g. cmake --install from a plain shell), so bundle the MSVC CRT explicitly.
# A clean Windows without the Visual C++ redistributable otherwise fails to
# start the portable build.
if(MSVC)
    get_filename_component(_msvc_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_level RANGE 1 6)
        get_filename_component(_msvc_compiler_dir "${_msvc_compiler_dir}" DIRECTORY)
    endforeach()
    file(GLOB _msvc_crt_dirs
        "${_msvc_compiler_dir}/Redist/MSVC/*/x64/Microsoft.VC143.CRT"
    )
    if(_msvc_crt_dirs)
        list(SORT _msvc_crt_dirs)
        list(GET _msvc_crt_dirs -1 _msvc_crt_dir)
        file(GLOB _msvc_crt_dlls "${_msvc_crt_dir}/*.dll")
        install(FILES ${_msvc_crt_dlls} DESTINATION ${CMAKE_INSTALL_BINDIR})
        message(STATUS "Bundling the MSVC runtime from ${_msvc_crt_dir}")
    else()
        message(WARNING "MSVC runtime redistributables were not found")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(QTRANSLATE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
