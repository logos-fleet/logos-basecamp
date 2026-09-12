# The Web container's end-to-end check, CONFIGURED ON ITS OWN.
#
# Its own project rather than a target in tests/CMakeLists.txt, for the reason
# logoscore configures src/webhost separately: this is the only thing in the
# repo that links Qt WebEngine, and keeping it out of the app's configure step
# is what lets `unit-tests`, `qml-tests` and the app itself stay byte-for-byte
# what they are while this exists. It compiles app/web/ — the shipping backend,
# not a copy of it — plus one driver.
cmake_minimum_required(VERSION 3.16)
project(LogosBasecampWebContainerTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

get_filename_component(BASECAMP_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../app" REALPATH)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test WebChannel WebEngineQuick WebEngineWidgets)

if(NOT DEFINED LOGOS_LIBLOGOS_ROOT)
    message(FATAL_ERROR "LOGOS_LIBLOGOS_ROOT must point at a logos-liblogos prefix "
                        "(it is where web_module_view.h and liblogos_core live).")
endif()
if(NOT DEFINED LOGOS_CPP_SDK_ROOT)
    message(FATAL_ERROR "LOGOS_CPP_SDK_ROOT must point at a logos-cpp-sdk prefix.")
endif()

find_package(logos-cpp-sdk REQUIRED CONFIG
    PATHS ${LOGOS_CPP_SDK_ROOT}/lib/cmake/logos-cpp-sdk
    NO_DEFAULT_PATH)

find_library(LOGOS_CORE_LIB
    NAMES logos_core
    PATHS ${LOGOS_LIBLOGOS_ROOT}/lib
    NO_DEFAULT_PATH
    REQUIRED)

add_executable(web_container_test
    main.cpp
    ${BASECAMP_APP_DIR}/web/LogosWebPaths.cpp
    ${BASECAMP_APP_DIR}/web/LogosWebScheme.cpp
    ${BASECAMP_APP_DIR}/web/WebModulePageView.cpp
    ${BASECAMP_APP_DIR}/web/WebContainerBackend.cpp
)

target_include_directories(web_container_test PRIVATE
    ${BASECAMP_APP_DIR}
    ${LOGOS_LIBLOGOS_ROOT}/include
    # web_module_view.h includes <message_channel.h>, which logos-protocol
    # installs one directory deeper. liblogos re-exports both, so one prefix
    # answers for both headers -- but only if the SECOND directory is named.
    ${LOGOS_LIBLOGOS_ROOT}/include/implementations/web
)

target_link_libraries(web_container_test PRIVATE
    Qt6::Widgets
    Qt6::Test
    Qt6::WebChannel
    Qt6::WebEngineQuick
    Qt6::WebEngineWidgets
    ${LOGOS_CORE_LIB}
    logos-cpp-sdk::logos_headers
)
