cmake_minimum_required(VERSION 3.16)
project(LogosBasecampUnitTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Test Widgets Quick QuickWidgets)

enable_testing()

# Three source roots: the host in ../app, the main_ui shell in ../src, and the
# phone hosts in ../mobile. The unit tests cover all three; ../app/interfaces
# holds the contract headers they share.
#
# ../mobile is here because the mobile Web container's decisions -- what a page
# may fetch, what a frame crossing the bridge looks like, how many QML runtimes
# may be alive -- are plain C++ with no UIKit and no JNI in them, and a phone is
# the worst possible place to find out one of them is wrong. The platform halves
# (the WKWebView, the Android WebView) are what is left over once these are
# tested here, and they are compiled by the iOS and Android stages.
set(BASECAMP_SRC    "${CMAKE_CURRENT_SOURCE_DIR}/../app")
set(BASECAMP_SHELL  "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(BASECAMP_MOBILE "${CMAKE_CURRENT_SOURCE_DIR}/../mobile")

if(DEFINED LOGOS_PACKAGE_HEADERS)
    set(LOGOS_PACKAGE_HEADERS_INCLUDE "${LOGOS_PACKAGE_HEADERS}")
endif()

# The intent classes include LogosIntent.h — the frozen vocabulary shared with
# the view-module runtime. It is header-only and pulls in nothing but QtCore, so
# adding the path costs the dependency-free harness nothing.
if(NOT DEFINED LOGOS_VIEW_MODULE_RUNTIME_ROOT)
    foreach(_candidate
            "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-view-module-runtime"
            "$ENV{LOGOS_VIEW_MODULE_RUNTIME_ROOT}")
        if(EXISTS "${_candidate}/include/LogosIntent.h")
            set(LOGOS_VIEW_MODULE_RUNTIME_ROOT "${_candidate}")
            break()
        endif()
    endforeach()
endif()
if(DEFINED LOGOS_VIEW_MODULE_RUNTIME_ROOT)
    set(LOGOS_INTENT_INCLUDE "${LOGOS_VIEW_MODULE_RUNTIME_ROOT}/include")
endif()

# The two SEAM headers the mobile Web container codes against: liblogos'
# WebModuleView (what a container is handed when it needs somewhere to run a
# page) and logos-protocol's IMessageChannel (what a page IS, to the transport).
# Headers only -- the mobile container tests stub the two process-globals those
# declare rather than linking a core, a loader and a transport into a check that
# drives none of them. Probed in both layouts, like every other root here: a
# source checkout has src/ and cpp/, a nix install has include/.
foreach(_candidate
        "${LOGOS_LIBLOGOS_ROOT}/src/logos_core"
        "${LOGOS_LIBLOGOS_ROOT}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-liblogos/src/logos_core")
    if(EXISTS "${_candidate}/web_module_view.h")
        set(LOGOS_WEB_SEAM_INCLUDE "${_candidate}")
        break()
    endif()
endforeach()
foreach(_candidate
        "${LOGOS_PROTOCOL_ROOT}/cpp/implementations/web"
        "${LOGOS_PROTOCOL_ROOT}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-protocol/cpp/implementations/web")
    if(EXISTS "${_candidate}/message_channel.h")
        set(LOGOS_MESSAGE_CHANNEL_INCLUDE "${_candidate}")
        break()
    endif()
endforeach()

# logos-plugin-qt's cpp/ -- the host side of the module teardown hook
# (logos_plugin_unload.{h,cpp}). A SOURCE root like the three above rather than
# a link: that file is pure QtCore, and view_mount_teardown_test compiles it in
# so the sequence it asserts is driven by the SHIPPING hook runner and not by a
# lookalike written for the test. Probed in both layouts, like every other root.
foreach(_candidate
        "${LOGOS_PLUGIN_QT_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../logos-plugin-qt")
    if(EXISTS "${_candidate}/cpp/logos_plugin_unload.cpp")
        set(LOGOS_PLUGIN_QT_SRC_ROOT "${_candidate}")
        set(LOGOS_PLUGIN_UNLOAD_INCLUDE "${_candidate}/cpp")
        break()
    endif()
endforeach()

# -----------------------------------------------------------------------
# Auto-discovery: any tests/*_test.cpp file is picked up automatically.
# -----------------------------------------------------------------------

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")

foreach(test_src IN LISTS TEST_SOURCES)
    get_filename_component(test_name "${test_src}" NAME_WE)

    # Read the top of the file and look for `// srcdeps: A.cpp B.cpp`.
    file(READ "${test_src}" _head LIMIT 1024)
    set(_extra_srcs "")
    if(_head MATCHES "//[ \t]*srcdeps:[ \t]*([^\n]*)")
        string(STRIP "${CMAKE_MATCH_1}" _deps_line)
        string(REGEX REPLACE "[ \t]+" ";" _dep_list "${_deps_line}")
        # Resolve a bare srcdep path against the host root, then the shell
        # root, then the mobile root. Missing in ALL THREE is fatal: dropping it
        # silently surfaces much later as an undefined reference from the test's
        # own object file.
        foreach(dep IN LISTS _dep_list)
            if(dep)
                if(EXISTS "${BASECAMP_SRC}/${dep}")
                    list(APPEND _extra_srcs "${BASECAMP_SRC}/${dep}")
                elseif(EXISTS "${BASECAMP_SHELL}/${dep}")
                    list(APPEND _extra_srcs "${BASECAMP_SHELL}/${dep}")
                elseif(EXISTS "${BASECAMP_MOBILE}/${dep}")
                    list(APPEND _extra_srcs "${BASECAMP_MOBILE}/${dep}")
                elseif(LOGOS_PLUGIN_QT_SRC_ROOT AND
                       EXISTS "${LOGOS_PLUGIN_QT_SRC_ROOT}/${dep}")
                    list(APPEND _extra_srcs "${LOGOS_PLUGIN_QT_SRC_ROOT}/${dep}")
                else()
                    message(FATAL_ERROR
                        "srcdep '${dep}' for ${test_name} not found under "
                        "${BASECAMP_SRC}, ${BASECAMP_SHELL}, ${BASECAMP_MOBILE} "
                        "or ${LOGOS_PLUGIN_QT_SRC_ROOT}")
                endif()
            endif()
        endforeach()
    endif()

    # AUTOMOC pairs a header with a .cpp of the same basename in the SAME
    # directory, and InstallEnums.cpp (src/) and its header (app/interfaces/)
    # are in different ones -- so the header rides along on every target to get
    # InstallStage/InstallStatus' staticMetaObject. Elsewhere: a spare moc TU.
    add_executable(${test_name} ${test_src} ${_extra_srcs}
        ${BASECAMP_SRC}/interfaces/InstallEnums.h)
    target_include_directories(${test_name} PRIVATE
        ${BASECAMP_SRC}
        ${BASECAMP_SRC}/interfaces
        ${BASECAMP_SHELL}
        ${BASECAMP_MOBILE}
        ${LOGOS_PACKAGE_HEADERS_INCLUDE}
        ${LOGOS_INTENT_INCLUDE}
        ${LOGOS_WEB_SEAM_INCLUDE}
        ${LOGOS_MESSAGE_CHANNEL_INCLUDE}
        ${LOGOS_PLUGIN_UNLOAD_INCLUDE}
    )
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickWidgets
        Qt6::Test
    )
    # THE REPO ROOT, for the gates that read SOURCE rather than link it.
    # `shell_backend_signal_contract_test` resolves the shared shell's
    # `Connections` handlers and MainContainer's string-named connects against
    # both backend headers -- a question about two files that cannot be linked
    # into one harness (ShellModulesBackend needs a core runtime), and one the
    # compiler answers for neither end of a SIGNAL() macro.
    target_compile_definitions(${test_name} PRIVATE
        BASECAMP_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
