cmake_minimum_required(VERSION 3.16)
project(LogosBasecampQmlTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)

# The design system ships its QML modules as STATIC qt_add_qml_module targets
# behind one umbrella, so a test that instantiates a real shell component has to
# link it — there is no filesystem import path to point at. Optional: the enum
# and proxy contract tests do not need it, and a design-system-less environment
# should still run those.
find_package(LogosDesignSystem CONFIG QUIET)

enable_testing()

# AppsModel and InstallRegistry are host-side; AppsFilterProxy and InstallEnums
# live in the main_ui shell. This suite covers the seam, so it builds from both.
set(BASECAMP_SRC   "${CMAKE_CURRENT_SOURCE_DIR}/../../app")
set(BASECAMP_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

if(DEFINED LOGOS_PACKAGE_HEADERS)
    set(LOGOS_PACKAGE_HEADERS_INCLUDE "${LOGOS_PACKAGE_HEADERS}")
endif()

add_executable(qml_tests
    main.cpp
    ${BASECAMP_SRC}/AppsModel.cpp
    ${BASECAMP_SRC}/InstallRegistry.cpp   # AppsModel depends on InstallRegistry
    ${BASECAMP_SHELL}/AppsFilterProxy.cpp
    ${BASECAMP_SHELL}/ModulesFilterProxy.cpp
    ${BASECAMP_SHELL}/InstallEnums.cpp
    # AUTOMOC pairs a header with a .cpp of the same basename in the SAME
    # directory; InstallEnums.cpp is in src/ and its header in app/interfaces/,
    # so it must be listed or the link fails on InstallStage/InstallStatus'
    # staticMetaObject.
    ${BASECAMP_SRC}/interfaces/InstallEnums.h
)
target_include_directories(qml_tests PRIVATE
    ${BASECAMP_SRC}
    ${BASECAMP_SRC}/interfaces
    ${BASECAMP_SHELL}
    ${LOGOS_PACKAGE_HEADERS_INCLUDE}
)
# ── The shell's own QML modules, as a filesystem import path ────────────────
# Basecamp.Settings and Basecamp.Common are qt_add_qml_module targets compiled
# into main_ui, so a test can neither `import` them nor reach them by relative
# path: a Settings view's own `import Basecamp.Common` has to resolve too.
# Mirroring each module's .qml into the build tree beside a generated qmldir
# gives the engine exactly that import path, and the views are then instantiated
# the way the shell instantiates them, imports and all.
#
# configure_file (not file(COPY)): it records a dependency on the source, so
# editing a shell .qml re-runs cmake instead of leaving a stale copy behind.
set(BASECAMP_QML_IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}/qml-imports")

function(basecamp_mirror_qml_module uri source_dir)
    string(REPLACE "." "/" rel "${uri}")
    set(out "${BASECAMP_QML_IMPORT_PATH}/${rel}")
    file(GLOB sources "${source_dir}/*.qml")
    set(qmldir "module ${uri}\n")
    foreach(qml ${sources})
        get_filename_component(base "${qml}" NAME_WE)
        get_filename_component(file "${qml}" NAME)
        configure_file("${qml}" "${out}/${file}" COPYONLY)
        # `pragma Singleton` is half a declaration: the engine refuses a type
        # that declares it and is not ALSO declared singleton in the qmldir
        # ("is a singleton, but was registered as a non-singleton"). The shell's
        # real qt_add_qml_module says so via QT_QML_SINGLETON_TYPE; a mirror
        # that dropped it would fail the moment a view imported a colour set.
        file(READ "${qml}" _contents)
        if(_contents MATCHES "pragma[ \t]+Singleton")
            string(APPEND qmldir "singleton ${base} 1.0 ${file}\n")
        else()
            string(APPEND qmldir "${base} 1.0 ${file}\n")
        endif()
    endforeach()
    file(WRITE "${out}/qmldir" "${qmldir}")
endfunction()

basecamp_mirror_qml_module(Basecamp.Common     "${BASECAMP_SHELL}/Basecamp/Common")
basecamp_mirror_qml_module(Basecamp.Settings   "${BASECAMP_SHELL}/Basecamp/Settings")
basecamp_mirror_qml_module(Basecamp.AppManager "${BASECAMP_SHELL}/Basecamp/AppManager")

if(LogosDesignSystem_FOUND)
    target_link_libraries(qml_tests PRIVATE Logos::DesignSystem)
    target_compile_definitions(qml_tests PRIVATE HAVE_LOGOS_DESIGN_SYSTEM=1)
    message(STATUS "qml_tests: design system found — shell component tests enabled")
else()
    message(STATUS "qml_tests: no design system — shell component tests skipped")
endif()

target_link_libraries(qml_tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickTest
    Qt6::Test
)
target_compile_definitions(qml_tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    BASECAMP_QML_IMPORT_PATH="${BASECAMP_QML_IMPORT_PATH}"
)

add_test(NAME qml_tests COMMAND qml_tests)
