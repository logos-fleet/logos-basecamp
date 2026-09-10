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
qt_add_executable(LiblogosSmoke
    ${SRC}/main.cpp
    ${SRC}/SmokeRunner.cpp ${SRC}/SmokeRunner.h)

foreach(_root IN LISTS LOGOS_INCLUDE_ROOTS)
    foreach(_inc include include/cpp include/core include/implementations/plain
                 include/logos_container include/logos_module_loader include/process_stats)
        if(IS_DIRECTORY "${_root}/${_inc}")
            target_include_directories(LiblogosSmoke PRIVATE "${_root}/${_inc}")
        endif()
    endforeach()
endforeach()

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
target_link_libraries(LiblogosSmoke PRIVATE
    ${_link} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::RemoteObjects)

set_target_properties(LiblogosSmoke PROPERTIES
    QT_ANDROID_PACKAGE_NAME "co.logos.liblogos.smoke"
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    QT_ANDROID_EXTRA_LIBS "${_extra}"
    QT_ANDROID_VERSION_CODE 1
    QT_ANDROID_VERSION_NAME "0.1")
