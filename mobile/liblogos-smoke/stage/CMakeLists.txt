# Pure half of the iOS smoke host: everything except main.cpp, built as a
# static archive by nix (pkgs.mkIosCmakeStage). The exported target carries
# every Logos and third-party archive as an absolute path, so the
# Xcode-generated app in ../app only has to link Logos::LiblogosSmokeHost.
#
# The split exists because an iOS .app is linked and signed by Xcode, outside
# nix. Everything that CAN be pure is, and the impure step gets one target.
cmake_minimum_required(VERSION 3.24)
project(LiblogosSmokeHost LANGUAGES CXX)

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
add_library(liblogos_smoke_host STATIC
    ${SRC}/SmokeRunner.cpp
    ${SRC}/SmokeRunner.h
    ${SRC}/BundledModuleRunner.cpp
    ${SRC}/BundledModuleRunner.h
    ${SRC}/ViewModuleRunner.cpp
    ${SRC}/ViewModuleRunner.h
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

# Which framework the runner dlopens. The nix expression that EMBEDS the
# bundle names it too; one string, passed in, so the two cannot disagree.
set(LOGOS_VIEW_MODULE_STEM "" CACHE STRING "Basename of the embedded view framework")
if(NOT LOGOS_VIEW_MODULE_STEM)
    message(FATAL_ERROR "LOGOS_VIEW_MODULE_STEM is unset.")
endif()
target_compile_definitions(liblogos_smoke_host
    PRIVATE LOGOS_VIEW_MODULE_STEM="${LOGOS_VIEW_MODULE_STEM}")
target_include_directories(liblogos_smoke_host PUBLIC
    $<BUILD_INTERFACE:${SRC}>
    $<INSTALL_INTERFACE:include/liblogos-smoke-host>)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosSmokeRoots.cmake)
logos_smoke_add_include_roots(liblogos_smoke_host PUBLIC "${LOGOS_IOS_INCLUDE_ROOTS}")

# The bundled module's manifest, compiled in -- see the header it generates.
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LogosBundledManifest.cmake)
logos_bundled_manifest(liblogos_smoke_host
    ${CMAKE_CURRENT_SOURCE_DIR}/../modules/bare_counter/manifest.json)

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
)

set_target_properties(liblogos_smoke_host PROPERTIES EXPORT_NAME LiblogosSmokeHost)
install(TARGETS liblogos_smoke_host EXPORT LiblogosSmokeHostTargets ARCHIVE DESTINATION lib)
install(FILES ${SRC}/SmokeRunner.h ${SRC}/BundledModuleRunner.h ${SRC}/ViewModuleRunner.h
        DESTINATION include/liblogos-smoke-host)
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
