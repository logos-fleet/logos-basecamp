#include "InstalledPackages.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace basecamp::shell {

namespace {

// The package's manifest, or an empty object when this directory does not hold
// one the way a package does. Everything below is a REASON to skip a directory
// rather than a failure: a modules directory is a place the user's installs
// land, and anything else that ends up in it must not take the sidebar down.
QJsonObject manifestOf(const QString& packageDir)
{
    QFile file(QDir(packageDir).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return { };
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

QStringList uiPackageNames(const QStringList& moduleDirs)
{
    QStringList names;
    for (const QString& dir : moduleDirs) {
        const QFileInfoList entries =
            QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& entry : entries) {
            // lgpm's staging and retired trees are dot-prefixed siblings of the
            // real install, and one can outlive a crash mid-swap holding a
            // valid manifest -- so it would name a package twice, once under a
            // directory nothing will ever load from.
            if (entry.fileName().startsWith(QLatin1Char('.'))) continue;

            const QJsonObject manifest = manifestOf(entry.absoluteFilePath());
            const QString name = manifest.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) continue;
            // ANYTHING BUT `core`, which is servesUiOf's rule and has to stay
            // its rule: the container decides from this same field whether the
            // page it opens is the module's UI, and a tile the container then
            // declares headless is a button onto a blank document.
            if (manifest.value(QStringLiteral("type")).toString() == QLatin1String("core"))
                continue;
            // One module, one tile: a user who installed a newer copy of
            // something the image ships has the same module in two directories.
            if (!names.contains(name)) names << name;
        }
    }
    return names;
}

} // namespace basecamp::shell
