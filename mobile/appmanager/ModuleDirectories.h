#pragma once

#include <QString>
#include <QStringList>

namespace basecamp::appmanager {

// WHERE A STORE SHELL'S MODULES COME FROM, AND WHERE AN INSTALL PUTS ONE.
//
// Three parties have to agree on one directory, and nothing made them:
//
//   * package_manager installs into whatever the Shell hands
//     setUserModulesDirectory;
//   * the core discovers modules ONLY in the directories it was given before
//     logos_core_start() -- adding one afterwards is not a thing the C ABI
//     offers;
//   * the Web container serves a module out of wherever the core found it.
//
// The Shell handed package_manager `<appData>/modules` while the core was
// scanning `<appData>/logos/modules`, so an install would succeed, report a
// path, and the module would never appear -- with no error anywhere, because
// each half did exactly what it was told.
//
// Both answers come from here, and the invariant between them is the thing a
// test can hold: `coreModulesDirs` CONTAINS `installModulesDir`.
struct ModuleDirectories {
    // Handed to ICoreRuntime::Config::modulesDirs, in scan order.
    QStringList coreModulesDirs;
    // Handed to package_manager.setUserModulesDirectory and
    // setUserUiPluginsDirectory.
    QString installModulesDir;
    QString installUiPluginsDir;

    // `appDataRoot` is QStandardPaths::AppDataLocation -- the app sandbox, and
    // on a phone the only place anything may be written (ADR 0003).
    //
    // `shippedWebModulesDir` is the app's BUILD-TIME `web-modules` tree: inside
    // the bundle on iOS, unpacked out of the APK on Android, and EMPTY for a
    // build that ships none. An empty one contributes no directory rather than
    // an empty string -- the core would take that as a directory and scan the
    // process's working directory.
    //
    // The shipped tree is scanned first and the writable one last, and they hold
    // disjoint names in a Store shell: the first is what this build embedded,
    // the second is what the user installed.
    static ModuleDirectories under(const QString& appDataRoot,
                                   const QString& shippedWebModulesDir = {});
};

} // namespace basecamp::appmanager
