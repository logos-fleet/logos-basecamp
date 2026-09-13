#include "ModuleDirectories.h"

#include <QDir>

namespace basecamp::appmanager {

ModuleDirectories ModuleDirectories::under(const QString& appDataRoot,
                                           const QString& shippedWebModulesDir)
{
    // `<appDataRoot>/logos` is the base SmokeRunner already creates and hands
    // the core (mobile/liblogos-smoke/src/SmokeRunner.cpp); naming the same
    // subtree here is what makes the install directory one the core scans
    // rather than a fourth place.
    const QDir base(QDir(appDataRoot).filePath(QStringLiteral("logos")));

    ModuleDirectories dirs;
    dirs.installModulesDir = base.filePath(QStringLiteral("modules"));
    dirs.installUiPluginsDir = base.filePath(QStringLiteral("ui-plugins"));

    if (!shippedWebModulesDir.isEmpty())
        dirs.coreModulesDirs << shippedWebModulesDir;
    dirs.coreModulesDirs << dirs.installModulesDir;
    return dirs;
}

} // namespace basecamp::appmanager
