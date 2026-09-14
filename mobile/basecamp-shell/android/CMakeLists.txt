# Android Basecamp Shell: the same Native-container host the probe next door
# runs, with Basecamp's REAL UI shell on top of it. Built by
# pkgs.mkQtAndroidApk (nix/android-apps.nix).
#
# One CMake project and no pure/impure split, unlike iOS: androiddeployqt and
# gradle both run inside the nix build, so the APK is one derivation. What IS
# the same as iOS is everything above the platform -- main.cpp, the shell
# resolved through QPluginLoader::staticInstances(), IShellHost answered by
# BundledSetShellHost, ICoreRuntime by BundledSetCoreRuntime -- because
# main_ui and the design system are cross-built as STATIC archives here too
# (nix/shell-ui-android.nix says why).
cmake_minimum_required(VERSION 3.24)
project(BasecampShellAndroid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects
    Qml Quick QuickWidgets QuickControls2 Svg)
qt_standard_project_setup(REQUIRES 6.5)
find_package(LogosDesignSystem CONFIG REQUIRED)
find_package(BasecampMainUI CONFIG REQUIRED)

set(LOGOS_LIB_ROOTS "" CACHE STRING "Prefixes holding lib/ of liblogos and its closure")
set(LOGOS_INCLUDE_ROOTS "" CACHE STRING "Prefixes holding include/ to compile against")

set(SHELL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(SMOKE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../liblogos-smoke/src)
set(SMOKE_CMAKE ${CMAKE_CURRENT_SOURCE_DIR}/../../liblogos-smoke/cmake)
set(BASECAMP_APP ${CMAKE_CURRENT_SOURCE_DIR}/../../../app)
# The App Manager's platform-free half, shared with the desktop unit tests and
# with the iOS stage. Same list on both phones, by construction.
set(BASECAMP_MOBILE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The Native-container host's own sources, the same list the probe and the iOS
# stage compile (see cmake/LogosMobileHostSources.cmake).
include(${SMOKE_CMAKE}/LogosMobileHostSources.cmake)
logos_mobile_host_sources(LOGOS_HOST_SOURCES)

# ModuleInstanceModel and CoreModuleManager are the DESKTOP app's, compiled in
# rather than reimplemented -- same reason as the iOS stage: the Modules tab
# binds one and drives the other, so a mobile copy of either would be a second
# definition of what a row is and of what "load" means.
#
# ViewModuleRunner is here because BundledSetShellHost mounts one, not because
# this platform has a view module to mount: the mobile catalog publishes no
# `ui_qml` variant for Android, so no Bundled set built here carries one and
# the Shell's app launcher comes up empty. The day a view .so exists, this is
# the file that opens it.
qt_add_executable(BasecampShell
    ${SHELL_SRC}/main.cpp
    ${LOGOS_HOST_SOURCES}
    ${SMOKE_SRC}/ViewModuleRunner.cpp ${SMOKE_SRC}/ViewModuleRunner.h
    ${SHELL_SRC}/ShellModuleRows.cpp ${SHELL_SRC}/ShellModuleRows.h
    ${SHELL_SRC}/ShellModulesBackend.cpp ${SHELL_SRC}/ShellModulesBackend.h
    ${SHELL_SRC}/BundledSetShellHost.cpp ${SHELL_SRC}/BundledSetShellHost.h
    ${SHELL_SRC}/ShellSceneDriver.cpp ${SHELL_SRC}/ShellSceneDriver.h
    ${SHELL_SRC}/ShellModulesDriver.cpp ${SHELL_SRC}/ShellModulesDriver.h
    ${SHELL_SRC}/ShellAppDriver.cpp ${SHELL_SRC}/ShellAppDriver.h
    ${SHELL_SRC}/ShellCatalogDriver.cpp ${SHELL_SRC}/ShellCatalogDriver.h
    ${SHELL_SRC}/ShellCallDriver.cpp ${SHELL_SRC}/ShellCallDriver.h
    ${SHELL_SRC}/ShellStoreBackend.cpp ${SHELL_SRC}/ShellStoreBackend.h
    ${BASECAMP_MOBILE}/appmanager/CatalogEntry.cpp ${BASECAMP_MOBILE}/appmanager/CatalogEntry.h
    ${BASECAMP_MOBILE}/appmanager/CatalogSource.cpp ${BASECAMP_MOBILE}/appmanager/CatalogSource.h
    ${BASECAMP_MOBILE}/appmanager/ConsentQueue.cpp ${BASECAMP_MOBILE}/appmanager/ConsentQueue.h
    ${BASECAMP_MOBILE}/appmanager/InstallGate.cpp ${BASECAMP_MOBILE}/appmanager/InstallGate.h
    ${BASECAMP_MOBILE}/appmanager/ModuleCallScript.cpp ${BASECAMP_MOBILE}/appmanager/ModuleCallScript.h
    ${BASECAMP_MOBILE}/appmanager/ModuleDirectories.cpp ${BASECAMP_MOBILE}/appmanager/ModuleDirectories.h
    ${BASECAMP_MOBILE}/appmanager/StoreAppManager.cpp ${BASECAMP_MOBILE}/appmanager/StoreAppManager.h
    ${BASECAMP_APP}/ModuleInstanceModel.cpp ${BASECAMP_APP}/ModuleInstanceModel.h
    ${BASECAMP_APP}/CoreModuleManager.cpp ${BASECAMP_APP}/CoreModuleManager.h
    ${BASECAMP_APP}/ModuleStatsCells.cpp ${BASECAMP_APP}/ModuleStatsCells.h)

include(${SMOKE_CMAKE}/LogosSmokeRoots.cmake)
logos_smoke_add_include_roots(BasecampShell PRIVATE "${LOGOS_INCLUDE_ROOTS}")
logos_mobile_host_includes(BasecampShell PRIVATE)
target_include_directories(BasecampShell PRIVATE ${SHELL_SRC} ${SMOKE_SRC} ${BASECAMP_MOBILE})

# LogosViewPlugin.h — the HOST side of the view-plugin interface, header only.
set(LOGOS_VIEW_RUNTIME_INCLUDE "" CACHE STRING "logos-view-module-runtime include/")
if(NOT LOGOS_VIEW_RUNTIME_INCLUDE)
    message(FATAL_ERROR
        "LOGOS_VIEW_RUNTIME_INCLUDE is unset; without LogosViewPlugin.h the host "
        "cannot cast the view plugin it constructs and the view module would "
        "silently never appear.")
endif()
target_include_directories(BasecampShell PRIVATE ${LOGOS_VIEW_RUNTIME_INCLUDE})

# Which image the view runner opens, as the module's stem. Empty on Android
# for the reason above -- a set here cannot carry a view module -- and the
# runner asks the manifest rather than assuming one is there.
target_compile_definitions(BasecampShell
    PRIVATE LOGOS_VIEW_MODULE_STEM="${LOGOS_VIEW_MODULE_STEM}")

# The Bundled SET's manifest, compiled in. Passed by nix, never read from the
# source tree: `--bundle` changes the set and must change no file here.
set(LOGOS_BUNDLED_SET_MANIFEST "" CACHE STRING "bundled-set.json for this build")
if(NOT LOGOS_BUNDLED_SET_MANIFEST)
    message(FATAL_ERROR
        "LOGOS_BUNDLED_SET_MANIFEST is unset; the host would compile in an empty "
        "set and report every module as missing at runtime.")
endif()
include(${SMOKE_CMAKE}/LogosBundledSetManifest.cmake)
logos_bundled_set_manifest(BasecampShell ${LOGOS_BUNDLED_SET_MANIFEST})

# The Logos libraries, and the directory androiddeployqt packages -- both the
# same as the probe's next door (see cmake/LogosAndroidHost.cmake).
include(${SMOKE_CMAKE}/LogosAndroidHost.cmake)
logos_android_link_logos(BasecampShell "${LOGOS_LIB_ROOTS}" LOGOS_APK_LIBS)

target_link_libraries(BasecampShell PRIVATE
    Basecamp::MainUI Logos::DesignSystem
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::RemoteObjects
    Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2 Qt6::Svg)

logos_android_package_source_dir(BasecampShell)

# ── the `web` half: the QML runtime a Downloaded module's page loads ──────
# Same contract, same macro-copied Java WebView, and the same reason as the
# probe next door (mobile/liblogos-smoke/android/CMakeLists.txt): an APK asset
# is NOT a file, so the tree is unpacked into the app's data directory on first
# launch and the STAMP is the store path's name, so a new build unpacks again
# and an unchanged one does not.
#
# The Shell ships it now because a Store shell INSTALLS `web` variants -- a
# phone may not download native code (ADR 0003) -- and without the runtime a
# module installed from the catalog has nowhere to run. The `web-modules`
# directory beside it is the build's own, and is scanned first; what the user
# installs lands in the writable one (appmanager/ModuleDirectories.h).
set(LOGOS_ANDROID_WEB_ASSETS "" CACHE STRING
    "Directory holding logos-runtime/ and web-modules/ to ship in the APK")
if(LOGOS_ANDROID_WEB_ASSETS)
    file(COPY ${LOGOS_ANDROID_WEB_ASSETS}/
         DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/android-package/assets/logos-web)
    get_filename_component(_web_assets_stamp "${LOGOS_ANDROID_WEB_ASSETS}" NAME)
else()
    set(_web_assets_stamp "none")
    message(WARNING
        "BasecampShell: no LOGOS_ANDROID_WEB_ASSETS -- the APK ships no QML "
        "runtime, so a module installed from the catalog has nowhere to run.")
endif()
target_compile_definitions(BasecampShell
    PRIVATE LOGOS_WEB_ASSETS_STAMP="${_web_assets_stamp}")

# ── the Qt QML modules, packaged at DEPLOY time ───────────────────────────
# androiddeployqt runs qmlimportscanner over these roots to decide which Qt
# QML modules the APK must carry. The Shell's QML is compiled bytecode inside
# main_ui's and the design system's archives, so the scan has to be pointed at
# the trees it came from -- the same fact the iOS app feeds to the static link
# through QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS. Without it the APK ships no
# QtQuick and the Shell comes up with `module "QtQuick" is not installed`.
set(BASECAMP_QML_SCAN_ROOTS "" CACHE STRING "QML source roots to scan for Qt module imports")
if(NOT BASECAMP_QML_SCAN_ROOTS)
    message(FATAL_ERROR
        "BASECAMP_QML_SCAN_ROOTS is unset; the scan would find no imports, the APK "
        "would carry no Qt QML module and the Shell would render nothing.")
endif()

# ── the SVG image-format plugin, named ────────────────────────────────────
# Every icon in the Shell is an SVG inside a qrc, and none of them rendered:
# the run logged fourteen `QQuickImage: Error decoding: .../warning.svg:
# Unsupported image format` and drew empty squares.
#
# The reason is the same one the iOS app has to work around, arriving by
# another route. Qt discovers a module's plugins by globbing
# `Qt6*PluginConfig.cmake` in ITS OWN cmake directory
# (__qt_internal_include_plugin_packages), and qsvg's config sits in the
# qtsvg prefix rather than qtbase's -- so Qt6::Gui's plugin list never
# mentions it. On iOS that means the archive is not linked; here it means the
# plugin is not in `android-deploy-plugins`, and androiddeployqt deploys
# exactly that list -- a `plugin_dir` entry in Qt6Gui's dependency XML is
# SKIPPED outright once the list is non-empty (androiddeployqt's
# readAndroidDependencyXml). The plugin was in the merged Qt prefix the whole
# time and nothing asked for it.
#
# QSvgIconPlugin beside it: the same prefix, the same glob, and it is what
# QIcon uses for an .svg.
include("${Qt6Svg_DIR}/../Qt6Gui/Qt6QSvgPluginConfig.cmake")
include("${Qt6Svg_DIR}/../Qt6Gui/Qt6QSvgIconPluginConfig.cmake")
set_property(TARGET BasecampShell APPEND PROPERTY QT_PLUGIN_TARGETS
    Qt6::QSvgPlugin Qt6::QSvgIconPlugin)

set_target_properties(BasecampShell PROPERTIES
    QT_ANDROID_PACKAGE_NAME "co.logos.basecamp.shell"
    QT_ANDROID_EXTRA_LIBS "${LOGOS_APK_LIBS}"
    QT_QML_ROOT_PATH "${BASECAMP_QML_SCAN_ROOTS}"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1")
