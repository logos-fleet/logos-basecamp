# Impure half: the Xcode-generator link of the iOS smoke host, driven by
# `nix run .#run-liblogos-smoke-ios-sim` / `...-ios-device`. Everything it
# links comes from the nix stage next door.
cmake_minimum_required(VERSION 3.24)
project(LiblogosSmokeIos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network RemoteObjects)
qt_standard_project_setup(REQUIRES 6.5)
find_package(LiblogosSmokeHost CONFIG REQUIRED)

qt_add_executable(LiblogosSmoke MACOSX_BUNDLE ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp)
target_link_libraries(LiblogosSmoke PRIVATE Logos::LiblogosSmokeHost Qt6::Widgets)

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
