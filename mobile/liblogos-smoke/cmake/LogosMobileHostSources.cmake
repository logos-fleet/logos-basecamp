# The mobile Native-container host's sources, as ONE list.
#
#   logos_mobile_host_sources(<out_var>)
#
# Everything that is "liblogos_core with a Bundled set in it, a log and the
# verdicts": the runners, the runtime seam the Modules tab binds to, and the
# Web container. It is a function rather than three copies of the same list
# because there are now three consumers -- the iOS stage, the Android probe
# and the Android Shell -- and a source added for one of them and forgotten in
# the others fails at link time in whichever host was not touched.
#
# NOT in the list, and each for a reason a caller has to make:
#
#   main.cpp             every host has its own, and that is the only
#                        difference between the probe and the Shell below the
#                        UI.
#   ViewModuleRunner     compiled by a host that MOUNTS a view module. The
#                        iOS stage and the Android Shell do; the Android probe
#                        renders no view (the mobile catalog publishes no
#                        ui_qml variant for Android, so no Bundled set there
#                        can carry one).
#   IosWebPage.mm        Objective-C++, and added by the iOS stage with its
#                        own ARC flags.
include_guard(GLOBAL)

function(logos_mobile_host_sources out_var)
    set(_src "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src")
    set(_webview "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../webview")
    set(_app "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../app")

    set(_sources
        ${_src}/SmokeRunner.cpp ${_src}/SmokeRunner.h
        ${_src}/BundledSetRunner.cpp ${_src}/BundledSetRunner.h
        ${_src}/NetworkSmokeRunner.cpp ${_src}/NetworkSmokeRunner.h
        ${_src}/BundledSetCoreRuntime.cpp ${_src}/BundledSetCoreRuntime.h
        # The Web container. Everything except the one platform page is the
        # same platform-free code the desktop unit tests drive.
        ${_app}/web/LogosWebPaths.cpp ${_app}/web/LogosWebPaths.h
        ${_webview}/MobileWebBridge.cpp ${_webview}/MobileWebBridge.h
        ${_webview}/MobileWebModuleView.cpp ${_webview}/MobileWebModuleView.h
        ${_webview}/LiveRuntimeBudget.cpp ${_webview}/LiveRuntimeBudget.h
        ${_webview}/MobileWebContainerBackend.cpp ${_webview}/MobileWebContainerBackend.h
        ${_webview}/WebPageProbe.cpp ${_webview}/WebPageProbe.h)

    if(ANDROID)
        # android.webkit.WebView over JNI -- the Android half of the container.
        list(APPEND _sources ${_webview}/AndroidWebPage.cpp ${_webview}/AndroidWebPage.h)
    endif()

    set(${out_var} "${_sources}" PARENT_SCOPE)
endfunction()

# The include roots every consumer of that list needs, whatever else it adds.
#
#   logos_mobile_host_includes(<target> <PUBLIC|PRIVATE>)
#
# app/interfaces for ICoreRuntime.h (Basecamp's own runtime seam, which this
# host implements), mobile/ so the container's headers are reached as
# `webview/...` and app/ so its one shared header is `web/LogosWebPaths.h` --
# the same spellings the desktop container and the unit tests use.
#
# BUILD_INTERFACE on all three, because one caller is an EXPORTED target: a
# source-tree path left in INTERFACE_INCLUDE_DIRECTORIES makes install(EXPORT)
# fail outright. It costs the two Android executables nothing.
function(logos_mobile_host_includes target visibility)
    target_include_directories(${target} ${visibility}
        $<BUILD_INTERFACE:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../app/interfaces>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../app>)
endfunction()
