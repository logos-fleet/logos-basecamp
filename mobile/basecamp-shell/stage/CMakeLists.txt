# Pure half of the iOS Basecamp Shell host: everything except main.cpp, built
# as a static archive by nix (pkgs.mkIosCmakeStage). The Xcode-generated app in
# ../app only has to link Basecamp::ShellHost.
#
# The split is the platform's: an iOS .app is linked and signed by Xcode, which
# cannot run inside the nix sandbox (ADR 0002). Same shape as
# mobile/liblogos-smoke/stage, and it reuses that stage rather than restating
# it -- Logos::LiblogosSmokeHost IS the mobile Native-container host (the core,
# its eight repos, BundledSetCoreRuntime and the Bundled-set manifest compiled
# in). What this adds is the Shell on top of it.
cmake_minimum_required(VERSION 3.24)
project(BasecampShellHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickWidgets QuickControls2)
find_package(LiblogosSmokeHost CONFIG REQUIRED)
find_package(LogosDesignSystem CONFIG REQUIRED)
find_package(BasecampMainUI CONFIG REQUIRED)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(BASECAMP_APP ${CMAKE_CURRENT_SOURCE_DIR}/../../../app)
# The App Manager's platform-free half, shared with the desktop unit tests --
# what a catalog row means, the order an install happens in, the consent queue.
# Its one seam (StoreAppManager::Backend) is implemented HERE, in
# ShellStoreBackend, which is the only file in it that includes logos-protocol.
set(BASECAMP_MOBILE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ModuleInstanceModel and CoreModuleManager are the DESKTOP app's, compiled in
# rather than reimplemented: the Modules tab binds one and drives the other, so
# a mobile copy of either would be a second definition of what a row is and of
# what "load" means. Both are plain Qt over ICoreRuntime -- CoreModuleManager's
# only other input is LogosAPI, for the per-module Interface drill-down.
add_library(basecamp_shell_host STATIC
    ${SRC}/ShellModuleRows.cpp
    ${SRC}/ShellModuleRows.h
    ${SRC}/InstalledPackages.cpp
    ${SRC}/InstalledPackages.h
    ${SRC}/ShellModulesBackend.cpp
    ${SRC}/ShellModulesBackend.h
    ${SRC}/BundledSetShellHost.cpp
    ${SRC}/BundledSetShellHost.h
    ${SRC}/WebAppSurface.cpp
    ${SRC}/WebAppSurface.h
    ${SRC}/QuickWidgetKeyboardFocus.cpp
    ${SRC}/QuickWidgetKeyboardFocus.h
    ${SRC}/ShellSceneDriver.cpp
    ${SRC}/ShellSceneDriver.h
    ${SRC}/ShellModulesDriver.cpp
    ${SRC}/ShellModulesDriver.h
    ${SRC}/ShellAppDriver.cpp
    ${SRC}/ShellAppDriver.h
    ${SRC}/ShellKeyboardDriver.cpp
    ${SRC}/ShellKeyboardDriver.h
    ${SRC}/ShellWebAppDriver.cpp
    ${SRC}/ShellWebAppDriver.h
    ${SRC}/ShellCatalogDriver.cpp
    ${SRC}/ShellCatalogDriver.h
    ${SRC}/ShellConsentDriver.cpp
    ${SRC}/ShellConsentDriver.h
    ${SRC}/ShellCallDriver.cpp
    ${SRC}/ShellCallDriver.h
    ${SRC}/ShellStoreBackend.cpp
    ${SRC}/ShellStoreBackend.h
    ${BASECAMP_MOBILE}/appmanager/CatalogEntry.cpp
    ${BASECAMP_MOBILE}/appmanager/CatalogEntry.h
    ${BASECAMP_MOBILE}/appmanager/CatalogSource.cpp
    ${BASECAMP_MOBILE}/appmanager/CatalogSource.h
    ${BASECAMP_MOBILE}/appmanager/ConsentQueue.cpp
    ${BASECAMP_MOBILE}/appmanager/ConsentQueue.h
    ${BASECAMP_MOBILE}/appmanager/ConsentScript.cpp
    ${BASECAMP_MOBILE}/appmanager/ConsentScript.h
    ${BASECAMP_MOBILE}/appmanager/InstallGate.cpp
    ${BASECAMP_MOBILE}/appmanager/InstallGate.h
    ${BASECAMP_MOBILE}/appmanager/ModuleCallScript.cpp
    ${BASECAMP_MOBILE}/appmanager/ModuleCallScript.h
    ${BASECAMP_MOBILE}/appmanager/ModuleDirectories.cpp
    ${BASECAMP_MOBILE}/appmanager/ModuleDirectories.h
    ${BASECAMP_MOBILE}/appmanager/StoreAppManager.cpp
    ${BASECAMP_MOBILE}/appmanager/StoreAppManager.h
    ${BASECAMP_APP}/ModuleInstanceModel.cpp
    ${BASECAMP_APP}/ModuleInstanceModel.h
    ${BASECAMP_APP}/CoreModuleManager.cpp
    ${BASECAMP_APP}/CoreModuleManager.h
    ${BASECAMP_APP}/ModuleStatsCells.cpp
    ${BASECAMP_APP}/ModuleStatsCells.h
)

target_include_directories(basecamp_shell_host PUBLIC
    $<BUILD_INTERFACE:${SRC}>
    $<BUILD_INTERFACE:${BASECAMP_MOBILE}>
    $<BUILD_INTERFACE:${BASECAMP_APP}>
    $<BUILD_INTERFACE:${BASECAMP_APP}/interfaces>
    $<INSTALL_INTERFACE:include/basecamp-shell-host>)

target_link_libraries(basecamp_shell_host PUBLIC
    Logos::LiblogosSmokeHost
    Basecamp::MainUI
    Logos::DesignSystem
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
)

set_target_properties(basecamp_shell_host PROPERTIES EXPORT_NAME ShellHost)
install(TARGETS basecamp_shell_host EXPORT BasecampShellHostTargets ARCHIVE DESTINATION lib)
install(FILES
    ${SRC}/ShellModuleRows.h
    ${SRC}/InstalledPackages.h
    ${SRC}/ShellModulesBackend.h
    ${SRC}/ShellStoreBackend.h
    ${SRC}/BundledSetShellHost.h
    ${SRC}/WebAppSurface.h
    ${SRC}/ShellSceneDriver.h
    ${SRC}/ShellModulesDriver.h
    ${SRC}/ShellAppDriver.h
    ${SRC}/ShellWebAppDriver.h
    ${SRC}/ShellCatalogDriver.h
    ${SRC}/ShellConsentDriver.h
    ${SRC}/ShellCallDriver.h
    ${BASECAMP_APP}/ModuleInstanceModel.h
    ${BASECAMP_APP}/CoreModuleManager.h
    ${BASECAMP_APP}/interfaces/IShellHost.h
    ${BASECAMP_APP}/interfaces/IShellView.h
    ${BASECAMP_APP}/interfaces/ICoreRuntime.h
    ${BASECAMP_APP}/interfaces/ShellSections.h
    ${BASECAMP_APP}/interfaces/InstallEnums.h
    ${BASECAMP_APP}/interfaces/BasecampModelRoles.h
    DESTINATION include/basecamp-shell-host)
install(FILES
    ${BASECAMP_MOBILE}/appmanager/CatalogEntry.h
    ${BASECAMP_MOBILE}/appmanager/CatalogSource.h
    ${BASECAMP_MOBILE}/appmanager/ConsentQueue.h
    ${BASECAMP_MOBILE}/appmanager/ConsentScript.h
    ${BASECAMP_MOBILE}/appmanager/InstallGate.h
    ${BASECAMP_MOBILE}/appmanager/ModuleCallScript.h
    ${BASECAMP_MOBILE}/appmanager/ModuleDirectories.h
    ${BASECAMP_MOBILE}/appmanager/StoreAppManager.h
    DESTINATION include/basecamp-shell-host/appmanager)
install(EXPORT BasecampShellHostTargets
    FILE BasecampShellHostTargets.cmake NAMESPACE Basecamp::
    DESTINATION lib/cmake/BasecampShellHost)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/BasecampShellHostConfig.cmake CONTENT [[
include(CMakeFindDependencyMacro)
find_dependency(Qt6 COMPONENTS Core Gui Widgets Qml Quick QuickWidgets QuickControls2)
find_dependency(LiblogosSmokeHost CONFIG)
find_dependency(LogosDesignSystem CONFIG)
find_dependency(BasecampMainUI CONFIG)
include("${CMAKE_CURRENT_LIST_DIR}/BasecampShellHostTargets.cmake")
]] @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/BasecampShellHostConfig.cmake
    DESTINATION lib/cmake/BasecampShellHost)
