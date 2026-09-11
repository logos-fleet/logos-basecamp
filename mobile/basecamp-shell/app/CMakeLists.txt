# Impure half: the Xcode-generator link of the iOS Basecamp Shell, driven by
# `nix run .#run-basecamp-shell-ios-sim` / `...-ios-device`. Everything it
# links comes from the nix stages next door.
cmake_minimum_required(VERSION 3.24)
project(BasecampShellIos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects Qml Quick
    QuickWidgets QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.5)
find_package(BasecampShellHost CONFIG REQUIRED)

qt_add_executable(BasecampShell MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp)
target_link_libraries(BasecampShell PRIVATE
    Basecamp::ShellHost Qt6::Widgets Qt6::QuickWidgets Qt6::QuickControls2)

# ── the Qt QML plugins, imported at LINK time ─────────────────────────────
# Qt for iOS is static, so `import QtQuick` resolves against plugins that are
# in the EXECUTABLE, not against anything on a plugin path. qmlimportscanner
# works that out from QML SOURCE, and the Shell's QML is compiled bytecode
# inside the archives -- so it is pointed at the trees they came from. Without
# this the app links, starts, and shows a blank pane with
# `module "QtQuick.Controls" is not installed`.
set(BASECAMP_QML_SCAN_ROOTS "" CACHE STRING "QML source roots to scan for Qt plugin imports")
if(NOT BASECAMP_QML_SCAN_ROOTS)
    message(FATAL_ERROR
        "BASECAMP_QML_SCAN_ROOTS is unset; the scan would find no imports and the "
        "Shell would render nothing at runtime.")
endif()
set(QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS ${BASECAMP_QML_SCAN_ROOTS})

# Qt registers default plugins by globbing its own prefix's Qt6Gui/ directory;
# qsvg's config sits in the qtsvg prefix, so it is never seen. Blank icons.
include("${Qt6Svg_DIR}/../Qt6Gui/Qt6QSvgPluginConfig.cmake")
# The _init object carries the Q_IMPORT_PLUGIN; without it the archive is
# dead-stripped and the sidebar icons come out empty.
target_link_libraries(BasecampShell PRIVATE Qt6::QSvgPlugin Qt6::QSvgPlugin_init)

# ── the Bundled set ───────────────────────────────────────────────────────
# Same contract as mobile/liblogos-smoke/app: a LIST of .framework bundles,
# embedded and signed with the app's identity by Xcode's copy phase, because
# <App>.app/Frameworks/ is the only directory an iOS app may carry a dylib in
# and dyld will not load an unsigned image (ADR 0003, 0007).
set(LOGOS_IOS_BUNDLED_FRAMEWORKS "" CACHE STRING
    "Semicolon-separated .framework bundles making up the Bundled set")
if(LOGOS_IOS_BUNDLED_FRAMEWORKS)
    foreach(_fw IN LISTS LOGOS_IOS_BUNDLED_FRAMEWORKS)
        if(NOT IS_DIRECTORY "${_fw}")
            message(FATAL_ERROR "Bundled-set member is not a directory: ${_fw}")
        endif()
        set_property(TARGET BasecampShell APPEND PROPERTY XCODE_EMBED_FRAMEWORKS "${_fw}")
    endforeach()
    set_target_properties(BasecampShell PROPERTIES
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY TRUE
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY TRUE
        # A framework's own install_name is @rpath/<bundle>/<binary>; without
        # this the app has no @rpath to resolve it against and every dlopen
        # fails with "image not found" no matter where the bundle sits.
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks")
else()
    message(WARNING
        "BasecampShell: no LOGOS_IOS_BUNDLED_FRAMEWORKS -- the Modules tab will "
        "list the manifest's modules and every one of them will fail to load.")
endif()

# ── the symbols the modules resolve upward ────────────────────────────────
# ADR 0006: a Bare module leaves every `lp_*` undefined and binds it to the app
# image at dlopen. In a fully static iOS app neither the force-load nor the
# export is a linker default; logos_ios_export_symbols() does both, off a list
# nix computes from the module images' own `nm -u` output.
set(LOGOS_IOS_CMAKE_DIR "" CACHE STRING "Directory holding LogosIosSymbolExports.cmake")
set(LOGOS_IOS_EXPORTED_SYMBOLS_FILE "" CACHE STRING "Newline-separated symbols the app must export")
if(LOGOS_IOS_CMAKE_DIR AND LOGOS_IOS_EXPORTED_SYMBOLS_FILE)
    include(${LOGOS_IOS_CMAKE_DIR}/LogosIosSymbolExports.cmake)
    logos_ios_export_symbols(BasecampShell SYMBOL_FILES ${LOGOS_IOS_EXPORTED_SYMBOLS_FILE})
elseif(LOGOS_IOS_BUNDLED_FRAMEWORKS)
    message(FATAL_ERROR
        "BasecampShell embeds a Bundled set but was given no exported-symbol list. "
        "The app would link and install and then fail at dlopen with undefined lp_*.")
endif()

set_target_properties(BasecampShell PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER co.logos.basecamp.shell
    MACOSX_BUNDLE_BUNDLE_NAME "Basecamp"
    MACOSX_BUNDLE_BUNDLE_VERSION 1
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER co.logos.basecamp.shell
)

# Empty for the simulator (unsigned); the device runner passes a real team id
# and lets Xcode provision automatically.
set(LOGOS_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team id for automatic signing")
if(LOGOS_IOS_DEVELOPMENT_TEAM)
    set_target_properties(BasecampShell PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${LOGOS_IOS_DEVELOPMENT_TEAM})
else()
    set_target_properties(BasecampShell PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()
