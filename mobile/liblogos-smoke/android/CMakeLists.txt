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
set(WEBVIEW ${CMAKE_CURRENT_SOURCE_DIR}/../../webview)
set(BASECAMP_APP ${CMAKE_CURRENT_SOURCE_DIR}/../../../app)
qt_add_executable(LiblogosSmoke
    ${SRC}/main.cpp
    ${SRC}/SmokeRunner.cpp ${SRC}/SmokeRunner.h
    ${SRC}/BundledSetRunner.cpp ${SRC}/BundledSetRunner.h
    ${SRC}/NetworkSmokeRunner.cpp ${SRC}/NetworkSmokeRunner.h
    ${SRC}/BundledSetCoreRuntime.cpp ${SRC}/BundledSetCoreRuntime.h
    # The Web container. Everything except AndroidWebPage.cpp is the same
    # platform-free code the iOS stage compiles and the desktop unit tests
    # drive; that one file is android.webkit.WebView over JNI.
    ${BASECAMP_APP}/web/LogosWebPaths.cpp ${BASECAMP_APP}/web/LogosWebPaths.h
    ${WEBVIEW}/MobileWebBridge.cpp ${WEBVIEW}/MobileWebBridge.h
    ${WEBVIEW}/MobileWebModuleView.cpp ${WEBVIEW}/MobileWebModuleView.h
    ${WEBVIEW}/LiveRuntimeBudget.cpp ${WEBVIEW}/LiveRuntimeBudget.h
    ${WEBVIEW}/MobileWebContainerBackend.cpp ${WEBVIEW}/MobileWebContainerBackend.h
    ${WEBVIEW}/WebPageProbe.cpp ${WEBVIEW}/WebPageProbe.h
    ${WEBVIEW}/AndroidWebPage.cpp ${WEBVIEW}/AndroidWebPage.h)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosSmokeRoots.cmake)
logos_smoke_add_include_roots(LiblogosSmoke PRIVATE "${LOGOS_INCLUDE_ROOTS}")

# ICoreRuntime.h -- Basecamp's own runtime seam, header-only. This host
# implements it (BundledSetCoreRuntime).
target_include_directories(LiblogosSmoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../app/interfaces
    # mobile/, so the Web container's headers are reached as `webview/...`, and
    # app/, so its one shared header is `web/LogosWebPaths.h` -- the same
    # spellings the desktop container, the iOS stage and the unit tests use.
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${BASECAMP_APP})

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

# Every .so under the roots travels in the APK; only the three the host calls
# directly are on the link line. A shared library that is merely dlopened by
# another one still has to be packaged, which is what the two lists separate.
set(_extra "")
foreach(_root IN LISTS LOGOS_LIB_ROOTS)
    file(GLOB _found "${_root}/lib/*.so" "${_root}/lib/*.so.*")
    list(APPEND _extra ${_found})
endforeach()
list(REMOVE_DUPLICATES _extra)
set(_link "")
foreach(_so IN LISTS _extra)
    if(_so MATCHES "liblogos_core\\.so$|liblogos_qt_host\\.so$|liblogos_protocol\\.so$")
        list(APPEND _link ${_so})
    endif()
endforeach()
if(NOT _link MATCHES "liblogos_core")
    message(FATAL_ERROR "liblogos_core.so not found under LOGOS_LIB_ROOTS: ${LOGOS_LIB_ROOTS}")
endif()
message(STATUS "Logos link: ${_link}")
message(STATUS "APK extra libs: ${_extra}")
# spdlog too, and it has to be the SAME libspdlog.so liblogos_core links: the
# host attaches a logcat sink to the core's channels (BundledSetRunner), and
# that only reaches them if there is one registry in the process. Compiled-lib
# mode is what makes the registry live in the .so rather than in each image.
set(_spdlog "")
foreach(_so IN LISTS _extra)
    if(_so MATCHES "libspdlog\\.so$")
        set(_spdlog ${_so})
    endif()
endforeach()
if(NOT _spdlog)
    message(FATAL_ERROR "libspdlog.so not found under LOGOS_LIB_ROOTS: ${LOGOS_LIB_ROOTS}")
endif()
target_compile_definitions(LiblogosSmoke PRIVATE SPDLOG_COMPILED_LIB)

target_link_libraries(LiblogosSmoke PRIVATE
    ${_link} ${_spdlog} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::RemoteObjects)

# THE ANDROID PACKAGE SOURCE DIR IS ASSEMBLED, not named: androiddeployqt takes
# exactly one, and this app needs two things in it -- its own manifest and icon,
# and the Web container's Java (mobile/webview/android/src). Copying beats
# moving the Java under mobile/liblogos-smoke/platform, because the same class
# is the Android half of the container for every host in this repo and belongs
# next to the C++ it is paired with.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/platform/
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/android-package)
file(COPY ${WEBVIEW}/android/src
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/android-package)

set_target_properties(LiblogosSmoke PROPERTIES
    QT_ANDROID_PACKAGE_NAME "co.logos.liblogos.smoke"
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/android-package"
    QT_ANDROID_EXTRA_LIBS "${_extra}"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1")
