# THE PHONE BRIDGE, DRIVEN BY A REAL BROWSER.
#
# Its own project rather than a target in tests/CMakeLists.txt, for the same
# reason tests/web-container has one: this and that are the only things in the
# repo that link Qt WebEngine, and keeping them out of the app's configure step
# is what lets `unit-tests`, `qml-tests` and the app stay what they are.
#
# It compiles mobile/webview/MobileWebBridge.cpp -- the shipping bridge, not a
# copy -- and nothing else of the container: what is under test is the half a
# phone's WKWebView or Android WebView would talk to, plus the JavaScript shim
# the bridge injects, which nothing on a desktop otherwise executes.
cmake_minimum_required(VERSION 3.16)
project(LogosBasecampMobileBridgeTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

get_filename_component(BASECAMP_APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../app" REALPATH)
get_filename_component(BASECAMP_MOBILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../mobile" REALPATH)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Test WebEngineCore WebEngineWidgets)

add_executable(mobile_bridge_test
    main.cpp
    ${BASECAMP_APP_DIR}/web/LogosWebPaths.cpp
    ${BASECAMP_MOBILE_DIR}/webview/MobileWebBridge.cpp
)

target_include_directories(mobile_bridge_test PRIVATE
    ${BASECAMP_APP_DIR}
    ${BASECAMP_MOBILE_DIR}
)

target_link_libraries(mobile_bridge_test PRIVATE
    Qt6::Widgets
    Qt6::Test
    Qt6::WebEngineCore
    Qt6::WebEngineWidgets
)
