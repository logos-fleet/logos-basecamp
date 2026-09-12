# Pure half of the iOS smoke host: everything except main.cpp, built as a
# static archive by nix (pkgs.mkIosCmakeStage). The exported target carries
# every Logos and third-party archive as an absolute path, so the
# Xcode-generated app in ../app only has to link Logos::LiblogosSmokeHost.
#
# The split exists because an iOS .app is linked and signed by Xcode, outside
# nix. Everything that CAN be pure is, and the impure step gets one target.
cmake_minimum_required(VERSION 3.24)
project(LiblogosSmokeHost LANGUAGES CXX)

# The Web container's iOS half is Objective-C++ (WKWebView, WKURLSchemeHandler),
# which is the one thing in this host that cannot be plain C++. Everything ELSE
# about that container -- what a page may fetch, how its frames cross, how many
# QML runtimes may live -- is plain C++ in mobile/webview/ and is tested on a
# desktop with no webview in it (tests/mobile_web_*_test.cpp).
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qml/Quick/QuickWidgets are here for the VIEW module: its QML is rendered in
# THIS process's engine (there is no ui-host subprocess on a phone), and the
# Qt symbols the module leaves undefined are resolved against these archives
# once the app force-loads and exports them.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects Qml Quick QuickWidgets)

# Prefixes whose lib/*.a are linked into the app and whose include/ this
# compiles against. Passed by nix as ;-lists -- naming store paths here would
# make the file unusable outside nix.
set(LOGOS_IOS_LIB_ROOTS "" CACHE STRING "Prefixes holding lib/*.a to link")
set(LOGOS_IOS_INCLUDE_ROOTS "" CACHE STRING "Prefixes holding include/ to compile against")

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(WEBVIEW ${CMAKE_CURRENT_SOURCE_DIR}/../../webview)
set(BASECAMP_APP ${CMAKE_CURRENT_SOURCE_DIR}/../../../app)
# The host's own sources, shared with the two Android hosts -- see
# cmake/LogosMobileHostSources.cmake for what is in the list. This stage adds
# ViewModuleRunner (it mounts the set's view module) and, below, the one
# Objective-C++ file the Web container needs on this platform.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosMobileHostSources.cmake)
logos_mobile_host_sources(LOGOS_HOST_SOURCES)
add_library(liblogos_smoke_host STATIC
    ${LOGOS_HOST_SOURCES}
    ${SRC}/ViewModuleRunner.cpp
    ${SRC}/ViewModuleRunner.h
    ${SRC}/WebModuleRunner.cpp
    ${SRC}/WebModuleRunner.h
)

# LogosViewPlugin.h — the HOST side of the view-plugin interface, from
# logos-view-module-runtime. Header only: the host constructs the view object
# itself, so there is nothing from that repo to link.
set(LOGOS_VIEW_RUNTIME_INCLUDE "" CACHE STRING "logos-view-module-runtime include/")
if(NOT LOGOS_VIEW_RUNTIME_INCLUDE)
    message(FATAL_ERROR
        "LOGOS_VIEW_RUNTIME_INCLUDE is unset; without LogosViewPlugin.h the host "
        "cannot cast the view plugin it constructs and the view module would "
        "silently never appear.")
endif()
target_include_directories(liblogos_smoke_host PRIVATE ${LOGOS_VIEW_RUNTIME_INCLUDE})

# ICoreRuntime.h -- Basecamp's own runtime seam, header-only. This host
# implements it (BundledSetCoreRuntime), which is what makes "the Modules tab
# over a Bundled set" the same interface the desktop shell already codes
# against rather than a mobile-only lookalike. Same three roots the Android
# hosts add, plus the installed-interface spelling only a stage needs.
logos_mobile_host_includes(liblogos_smoke_host PUBLIC)
target_include_directories(liblogos_smoke_host PUBLIC
    $<BUILD_INTERFACE:${SRC}>
    $<INSTALL_INTERFACE:include/liblogos-smoke-host>)

# Which framework the view runner dlopens. Derived by nix from the SAME
# Bundled-set resolution that produced the embed list, so the two cannot
# disagree. Empty is legal and means this set carries no view module --
# `--bundle bare_counter` alone is a perfectly good Bundled set, and the host
# asks the manifest rather than assuming one is there.
target_compile_definitions(liblogos_smoke_host
    PRIVATE LOGOS_VIEW_MODULE_STEM="${LOGOS_VIEW_MODULE_STEM}")

if(IOS)
    target_sources(liblogos_smoke_host PRIVATE
        ${WEBVIEW}/IosWebPage.mm
        ${WEBVIEW}/IosWebPage.h)
    # ARC is not the default for a .mm compiled by CMake, and this file is
    # written for it: the holder that owns the webview, the handler and the
    # delegate is a C++ struct with strong Objective-C members.
    set_source_files_properties(${WEBVIEW}/IosWebPage.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(liblogos_smoke_host PUBLIC
        "-framework WebKit"
        "-framework UIKit")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosSmokeRoots.cmake)
logos_smoke_add_include_roots(liblogos_smoke_host PUBLIC "${LOGOS_IOS_INCLUDE_ROOTS}")

# The Bundled SET's manifest, compiled in -- see the header it generates. It is
# the host's whole knowledge of what it carries, so it is passed in by nix
# rather than read from the source tree: `--bundle` changes the set and must
# change no file here.
set(LOGOS_BUNDLED_SET_MANIFEST "" CACHE STRING "bundled-set.json for this build")
if(NOT LOGOS_BUNDLED_SET_MANIFEST)
    message(FATAL_ERROR
        "LOGOS_BUNDLED_SET_MANIFEST is unset; the host would compile in an empty "
        "set and report every module as missing at runtime.")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosBundledSetManifest.cmake)
logos_bundled_set_manifest(liblogos_smoke_host ${LOGOS_BUNDLED_SET_MANIFEST})

# A flat list of archives is enough: ld-prime resolves across archives in any
# order. liblogos_core goes first anyway, so its undefined symbols are what
# pull the rest in and a missing archive shows up as ITS symbol, not as a
# random one from the middle of the closure.
set(_archives "")
foreach(_root IN LISTS LOGOS_IOS_LIB_ROOTS)
    file(GLOB _found "${_root}/lib/*.a")
    list(APPEND _archives ${_found})
endforeach()
# liblgx.a is the whole C ABI including lgx_core; linking both duplicates it.
list(FILTER _archives EXCLUDE REGEX "liblgx_core\\.a$")
set(_core "")
foreach(_a IN LISTS _archives)
    if(_a MATCHES "liblogos_core\\.a$")
        set(_core ${_a})
    endif()
endforeach()
if(NOT _core)
    message(FATAL_ERROR "liblogos_core.a not found under LOGOS_IOS_LIB_ROOTS: ${LOGOS_IOS_LIB_ROOTS}")
endif()
list(REMOVE_ITEM _archives ${_core})
list(PREPEND _archives ${_core})
message(STATUS "Logos archives: ${_archives}")

target_link_libraries(liblogos_smoke_host PUBLIC
    ${_archives}
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::RemoteObjects
    Qt6::Qml Qt6::Quick Qt6::QuickWidgets
    z                            # lgx's gzip handler; libz is in the iOS SDK
    "-framework CoreFoundation"  # LGX_UNICODE_COREFOUNDATION
    "-framework Foundation"
    # SecRandomCopyBytes / CCRandomGenerateBytes. A Bare module carries every
    # third-party library inside its own image, but NOT the system ones: iOS has
    # no /dev/urandom a sandboxed app may open, so zerokit's rln and the chat
    # core reach the kernel CSPRNG through Security.framework and leave those two
    # symbols for the app to resolve. Named here rather than discovered, because
    # an iOS framework is linked `-undefined dynamic_lookup` and a missing one is
    # not a link error -- it is a crash at the first key generation.
    "-framework Security"
)

set_target_properties(liblogos_smoke_host PROPERTIES EXPORT_NAME LiblogosSmokeHost)
install(TARGETS liblogos_smoke_host EXPORT LiblogosSmokeHostTargets ARCHIVE DESTINATION lib)
install(FILES ${SRC}/SmokeRunner.h ${SRC}/BundledSetRunner.h ${SRC}/NetworkSmokeRunner.h
        ${SRC}/BundledSetCoreRuntime.h ${SRC}/ViewModuleRunner.h
        ${SRC}/WebModuleRunner.h
        # Header-only, and it is the APP's main.cpp that includes it -- so it
        # has to travel in the installed interface like the runners do.
        ${SRC}/PlatformConsole.h
        ${BASECAMP_APP}/interfaces/ICoreRuntime.h
        DESTINATION include/liblogos-smoke-host)
# The Web container's headers keep their DIRECTORY, so `webview/...` and
# `web/...` mean the same thing to the app above this stage as they do to the
# desktop container and the unit tests. A flat install would make the iOS app
# spell two includes differently from everything else that compiles them.
install(FILES
        ${WEBVIEW}/MobileWebBridge.h
        ${WEBVIEW}/MobileWebModuleView.h
        ${WEBVIEW}/MobileWebContainerBackend.h
        ${WEBVIEW}/LiveRuntimeBudget.h
        ${WEBVIEW}/WebPageProbe.h
        ${WEBVIEW}/AppMemory.h
        DESTINATION include/liblogos-smoke-host/webview)
install(FILES ${BASECAMP_APP}/web/LogosWebPaths.h
        DESTINATION include/liblogos-smoke-host/web)
if(IOS)
    install(FILES ${WEBVIEW}/IosWebPage.h DESTINATION include/liblogos-smoke-host/webview)
endif()
install(EXPORT LiblogosSmokeHostTargets
    FILE LiblogosSmokeHostTargets.cmake NAMESPACE Logos::
    DESTINATION lib/cmake/LiblogosSmokeHost)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/LiblogosSmokeHostConfig.cmake CONTENT [[
include(CMakeFindDependencyMacro)
find_dependency(Qt6 COMPONENTS Core Gui Widgets Network RemoteObjects Qml Quick QuickWidgets)
include("${CMAKE_CURRENT_LIST_DIR}/LiblogosSmokeHostTargets.cmake")
]] @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LiblogosSmokeHostConfig.cmake
    DESTINATION lib/cmake/LiblogosSmokeHost)
