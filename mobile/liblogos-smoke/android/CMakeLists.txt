# Android smoke host: one Qt app that links the shared liblogos_core and
# bundles its whole .so closure into the APK through QT_ANDROID_EXTRA_LIBS.
# Built by pkgs.mkQtAndroidApk.
#
# There is no pure/impure split here, unlike iOS: androiddeployqt and gradle
# both run inside the nix build, so the APK is one derivation.
cmake_minimum_required(VERSION 3.24)
project(LiblogosSmokeAndroid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects)
qt_standard_project_setup(REQUIRES 6.5)

set(LOGOS_LIB_ROOTS "" CACHE STRING "Prefixes holding lib/ of liblogos and its closure")
set(LOGOS_INCLUDE_ROOTS "" CACHE STRING "Prefixes holding include/ to compile against")

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
# The host's own sources, shared with the iOS stage and with the Android Shell
# next door -- see cmake/LogosMobileHostSources.cmake for what is in the list
# and what each host adds for itself. This one adds main.cpp and the runner that
# drives the app's Downloaded `web` modules, which is the probe's acceptance
# pass and not something the Shell does.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosMobileHostSources.cmake)
logos_mobile_host_sources(LOGOS_HOST_SOURCES)
qt_add_executable(LiblogosSmoke
    ${SRC}/main.cpp
    ${LOGOS_HOST_SOURCES}
    ${SRC}/WebModuleRunner.cpp ${SRC}/WebModuleRunner.h)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosSmokeRoots.cmake)
logos_smoke_add_include_roots(LiblogosSmoke PRIVATE "${LOGOS_INCLUDE_ROOTS}")

logos_mobile_host_includes(LiblogosSmoke PRIVATE)

# The Bundled SET's manifest, compiled in. Passed by nix, never read from the
# source tree: `--bundle` changes the set and must change no file here.
set(LOGOS_BUNDLED_SET_MANIFEST "" CACHE STRING "bundled-set.json for this build")
if(NOT LOGOS_BUNDLED_SET_MANIFEST)
    message(FATAL_ERROR
        "LOGOS_BUNDLED_SET_MANIFEST is unset; the host would compile in an empty "
        "set and report every module as missing at runtime.")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosBundledSetManifest.cmake)
logos_bundled_set_manifest(LiblogosSmoke ${LOGOS_BUNDLED_SET_MANIFEST})

# The Logos libraries, and the directory androiddeployqt packages -- both the
# same as the Shell's next door (see cmake/LogosAndroidHost.cmake).
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosAndroidHost.cmake)
logos_android_link_logos(LiblogosSmoke "${LOGOS_LIB_ROOTS}" LOGOS_APK_LIBS)

target_link_libraries(LiblogosSmoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::RemoteObjects)

logos_android_package_source_dir(LiblogosSmoke)

# ── the `web` half: the QML runtime and the Downloaded `web` modules ──────
# Into the package source dir's `assets/`, which is what gradle packages and
# what an APK can carry a directory tree in at all -- a .so is the only other
# thing an APK holds, and these are wasm and JavaScript.
#
# AN ASSET IS NOT A FILE, which is why the app unpacks them on first launch
# (unpackAndroidWebAssets): Qt reads `assets:/` through a virtual file engine
# that cannot answer canonicalFilePath(), and canonical-on-both-sides is how
# LogosWebPaths refuses a traversal out of a package. The STAMP is the store
# path's name, so a new app build unpacks again and an unchanged one does not.
set(LOGOS_ANDROID_WEB_ASSETS "" CACHE STRING
    "Directory holding logos-runtime/ and web-modules/ to ship in the APK")
if(LOGOS_ANDROID_WEB_ASSETS)
    file(COPY ${LOGOS_ANDROID_WEB_ASSETS}/
         DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/android-package/assets/logos-web)
    get_filename_component(_web_assets_stamp "${LOGOS_ANDROID_WEB_ASSETS}" NAME)
else()
    set(_web_assets_stamp "none")
    message(WARNING
        "LiblogosSmoke: no LOGOS_ANDROID_WEB_ASSETS -- the APK ships no QML "
        "runtime and no `web` module, so the Web container has nothing to load.")
endif()
target_compile_definitions(LiblogosSmoke
    PRIVATE LOGOS_WEB_ASSETS_STAMP="${_web_assets_stamp}")

set_target_properties(LiblogosSmoke PROPERTIES
    QT_ANDROID_PACKAGE_NAME "co.logos.liblogos.smoke"
    QT_ANDROID_EXTRA_LIBS "${LOGOS_APK_LIBS}"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1")
