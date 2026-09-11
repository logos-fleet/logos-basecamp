# Impure half: the Xcode-generator link of the iOS smoke host, driven by
# `nix run .#run-liblogos-smoke-ios-sim` / `...-ios-device`. Everything it
# links comes from the nix stage next door.
cmake_minimum_required(VERSION 3.24)
project(LiblogosSmokeIos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects Qml Quick QuickWidgets)
qt_standard_project_setup(REQUIRES 6.5)
find_package(LiblogosSmokeHost CONFIG REQUIRED)

qt_add_executable(LiblogosSmoke MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp)
target_link_libraries(LiblogosSmoke PRIVATE Logos::LiblogosSmokeHost Qt6::Widgets Qt6::QuickWidgets)

# ── the QML plugins, imported at LINK time ────────────────────────────────
# Qt for iOS is static, so `import QtQuick` resolves against plugins that are
# in the EXECUTABLE, not against anything on a plugin path. Linking a Qt6 static
# QML plugin target also emits its Q_IMPORT_PLUGIN, which is what makes the
# import visible to the engine. The view module's Main.qml imports QtQuick and
# nothing else, which is why this list is three entries and not thirty.
foreach(_qml_plugin
        Qt6::qmlplugin              # QtQml
        Qt6::modelsplugin           # QtQml.Models, which QtQuick requires
        Qt6::qtquick2plugin)        # QtQuick
    if(NOT TARGET ${_qml_plugin})
        message(FATAL_ERROR
            "${_qml_plugin} is not available in this Qt. A statically linked iOS "
            "app resolves `import QtQuick` against plugins linked INTO it, so "
            "without this one the view module's QML fails to load at runtime "
            "with `module \"QtQuick\" is not installed`.")
    endif()
    target_link_libraries(LiblogosSmoke PRIVATE ${_qml_plugin})
endforeach()

# ── the Bundled Bare module ───────────────────────────────────────────────
# The counter's iOS framework, embedded and signed with the app's identity by
# Xcode's copy phase. <App>.app/Frameworks/ is the only directory an iOS app
# may carry a dylib in, and dyld will not load an image that is not signed --
# which is why the module cannot be staged into the sandbox at runtime.
set(LOGOS_IOS_BARE_MODULE_FRAMEWORK "" CACHE STRING
    "Path to the Bare module's .framework bundle to embed")
if(LOGOS_IOS_BARE_MODULE_FRAMEWORK)
    if(NOT IS_DIRECTORY "${LOGOS_IOS_BARE_MODULE_FRAMEWORK}")
        message(FATAL_ERROR
            "LOGOS_IOS_BARE_MODULE_FRAMEWORK is not a directory: ${LOGOS_IOS_BARE_MODULE_FRAMEWORK}")
    endif()
    set_property(TARGET LiblogosSmoke APPEND PROPERTY
        XCODE_EMBED_FRAMEWORKS "${LOGOS_IOS_BARE_MODULE_FRAMEWORK}")
    set_target_properties(LiblogosSmoke PROPERTIES
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY TRUE
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY TRUE
        # The framework's own install_name is @rpath/<bundle>/<binary>; without
        # this the app has no @rpath to resolve it against and every dlopen
        # fails with "image not found" no matter where the bundle sits.
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks")
else()
    message(WARNING
        "LiblogosSmoke: no LOGOS_IOS_BARE_MODULE_FRAMEWORK -- the app will start "
        "the core and then report the bundled module as missing.")
endif()

# ── the Bundled VIEW module ───────────────────────────────────────────────
# The same embed-and-sign arrangement, for the ui_qml counter: its framework
# holds the Qt backend and the QML, and binds Qt upward into this app.
set(LOGOS_IOS_VIEW_MODULE_FRAMEWORK "" CACHE STRING
    "Path to the view module's .framework bundle to embed")
if(LOGOS_IOS_VIEW_MODULE_FRAMEWORK)
    if(NOT IS_DIRECTORY "${LOGOS_IOS_VIEW_MODULE_FRAMEWORK}")
        message(FATAL_ERROR
            "LOGOS_IOS_VIEW_MODULE_FRAMEWORK is not a directory: ${LOGOS_IOS_VIEW_MODULE_FRAMEWORK}")
    endif()
    set_property(TARGET LiblogosSmoke APPEND PROPERTY
        XCODE_EMBED_FRAMEWORKS "${LOGOS_IOS_VIEW_MODULE_FRAMEWORK}")
else()
    message(WARNING
        "LiblogosSmoke: no LOGOS_IOS_VIEW_MODULE_FRAMEWORK -- the app will start "
        "and then report the view module as missing.")
endif()

# ── the symbols the module resolves upward ────────────────────────────────
# ADR 0006: a Bare module leaves every `lp_*` undefined and binds it to the app
# image at dlopen. In a fully static iOS app neither half of that is a linker
# default -- the archive member is not pulled in unless something references
# it, and an unlisted symbol is not exported. logos_ios_export_symbols() does
# both, off a list nix computes from the module's own `nm -u` output.
set(LOGOS_IOS_CMAKE_DIR "" CACHE STRING "Directory holding LogosIosSymbolExports.cmake")
set(LOGOS_IOS_EXPORTED_SYMBOLS_FILE "" CACHE STRING "Newline-separated symbols the app must export")
if(LOGOS_IOS_CMAKE_DIR AND LOGOS_IOS_EXPORTED_SYMBOLS_FILE)
    include(${LOGOS_IOS_CMAKE_DIR}/LogosIosSymbolExports.cmake)
    logos_ios_export_symbols(LiblogosSmoke SYMBOL_FILES ${LOGOS_IOS_EXPORTED_SYMBOLS_FILE})
elseif(LOGOS_IOS_BARE_MODULE_FRAMEWORK)
    message(FATAL_ERROR
        "LiblogosSmoke embeds a Bare module but was given no exported-symbol list. "
        "The app would link and install and then fail at dlopen with undefined lp_*.")
endif()

set_target_properties(LiblogosSmoke PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER co.logos.liblogos.smoke
    MACOSX_BUNDLE_BUNDLE_NAME "Liblogos Smoke"
    MACOSX_BUNDLE_BUNDLE_VERSION 1
    MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER co.logos.liblogos.smoke
)

# Empty for the simulator (unsigned); the device runner passes a real team id
# and lets Xcode provision automatically.
set(LOGOS_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team id for automatic signing")
if(LOGOS_IOS_DEVELOPMENT_TEAM)
    set_target_properties(LiblogosSmoke PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ${LOGOS_IOS_DEVELOPMENT_TEAM})
else()
    set_target_properties(LiblogosSmoke PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
endif()
