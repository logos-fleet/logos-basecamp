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

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects)

# Prefixes whose lib/*.a are linked into the app and whose include/ this
# compiles against. Passed by nix as ;-lists -- naming store paths here would
# make the file unusable outside nix.
set(LOGOS_IOS_LIB_ROOTS "" CACHE STRING "Prefixes holding lib/*.a to link")
set(LOGOS_IOS_INCLUDE_ROOTS "" CACHE STRING "Prefixes holding include/ to compile against")

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_library(liblogos_smoke_host STATIC
    ${SRC}/SmokeRunner.cpp
    ${SRC}/SmokeRunner.h
)
target_include_directories(liblogos_smoke_host PUBLIC
    $<BUILD_INTERFACE:${SRC}>
    $<INSTALL_INTERFACE:include/liblogos-smoke-host>)

# Each Logos stage installs its headers under a different subdirectory of its
# prefix (the repos disagree, and this is not the place to change that).
foreach(_root IN LISTS LOGOS_IOS_INCLUDE_ROOTS)
    foreach(_inc include include/cpp include/core include/implementations/plain
                 include/logos_container include/logos_module_loader include/process_stats)
        if(IS_DIRECTORY "${_root}/${_inc}")
            target_include_directories(liblogos_smoke_host PUBLIC "${_root}/${_inc}")
        endif()
    endforeach()
endforeach()

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
    z                            # lgx's gzip handler; libz is in the iOS SDK
    "-framework CoreFoundation"  # LGX_UNICODE_COREFOUNDATION
    "-framework Foundation"
)

set_target_properties(liblogos_smoke_host PROPERTIES EXPORT_NAME LiblogosSmokeHost)
install(TARGETS liblogos_smoke_host EXPORT LiblogosSmokeHostTargets ARCHIVE DESTINATION lib)
install(FILES ${SRC}/SmokeRunner.h DESTINATION include/liblogos-smoke-host)
install(EXPORT LiblogosSmokeHostTargets
    FILE LiblogosSmokeHostTargets.cmake NAMESPACE Logos::
    DESTINATION lib/cmake/LiblogosSmokeHost)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/LiblogosSmokeHostConfig.cmake CONTENT [[
include(CMakeFindDependencyMacro)
find_dependency(Qt6 COMPONENTS Core Gui Widgets Network RemoteObjects)
include("${CMAKE_CURRENT_LIST_DIR}/LiblogosSmokeHostTargets.cmake")
]] @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/LiblogosSmokeHostConfig.cmake
    DESTINATION lib/cmake/LiblogosSmokeHost)
