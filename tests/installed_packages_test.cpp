// srcdeps: basecamp-shell/src/InstalledPackages.cpp
//
// WHICH OF THE PACKAGES ON THIS DEVICE HAVE A USER INTERFACE, read off disk
// while nothing is running.
//
// A Store shell's sidebar used to be able to answer that only for a module the
// Web container had already opened a page for -- which is the module the
// install just brought, and nothing else. On every later launch the core
// discovers the same package in the same directory and waits to be asked, so
// the app the user installed had no tile (#123).
//
// The evidence that does not need a page is the package's own manifest.json:
// package_manager wrote it beside the module at install time, the app image
// carries one beside every shipped `web` module, and `type` in it is the SAME
// field the container reads to decide whether a page serves a UI
// (MobileWebModuleView's servesUiOf). Reading it here is what lets the Shell
// draw the tile first and load the module when it is pressed.
//
// Tested off a real directory tree rather than a stub: what this unit has to
// get right is the shapes a modules directory really holds -- lgpm's dot-
// prefixed staging trees, a half-written manifest, a directory whose name is
// not the package's.
//
// Run: nix build .#unit-tests -L

#include "basecamp-shell/src/InstalledPackages.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using basecamp::shell::uiPackageNames;

namespace {

// One installed package, as package_manager leaves it: a directory with a
// manifest.json in it.
void writePackage(const QString& dir, const QString& dirName, const QJsonObject& manifest)
{
    QDir(dir).mkpath(dirName);
    QFile file(QDir(dir).filePath(dirName + QStringLiteral("/manifest.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(manifest).toJson());
}

QJsonObject manifest(const QString& name, const QString& type)
{
    QJsonObject object;
    object[QStringLiteral("name")] = name;
    if (!type.isNull()) object[QStringLiteral("type")] = type;
    object[QStringLiteral("version")] = QStringLiteral("1.0.0");
    return object;
}

} // namespace

class InstalledPackagesTest : public QObject {
    Q_OBJECT

private slots:
    void anInstalledUiPackageIsNamed()
    {
        QTemporaryDir modules;
        writePackage(modules.path(), QStringLiteral("web_counter"),
                     manifest(QStringLiteral("web_counter"), QStringLiteral("ui_qml")));

        QCOMPARE(uiPackageNames({ modules.path() }),
                 QStringList{ QStringLiteral("web_counter") });
    }

    void aCorePackageIsNot()
    {
        // A `core` module's `web` variant has a page -- a wasm image needs a
        // document -- and no user interface. The container says so from the
        // same field, and a tile onto its blank page is the defect that made
        // `type` load-bearing in the first place.
        QTemporaryDir modules;
        writePackage(modules.path(), QStringLiteral("web_indexer"),
                     manifest(QStringLiteral("web_indexer"), QStringLiteral("core")));

        QVERIFY(uiPackageNames({ modules.path() }).isEmpty());
    }

    void aManifestWithNoTypeReadsAsAUi()
    {
        // Exactly what MobileWebModuleView::servesUiOf does with one, and for
        // the same reason: every package written before a `core` `web` variant
        // existed has no `type`, and all of them are views.
        QTemporaryDir modules;
        writePackage(modules.path(), QStringLiteral("old_counter"),
                     manifest(QStringLiteral("old_counter"), QString()));

        QCOMPARE(uiPackageNames({ modules.path() }),
                 QStringList{ QStringLiteral("old_counter") });
    }

    void theNameIsTheManifestsAndNotTheDirectorys()
    {
        // The identity the core registers a package under is the manifest's
        // (ModuleRegistry::discoverInstalledModules binds it to the trusted
        // package name), so a set keyed by directory name would not intersect
        // the core's known modules at all.
        QTemporaryDir modules;
        writePackage(modules.path(), QStringLiteral("web_counter-1.2.0"),
                     manifest(QStringLiteral("web_counter"), QStringLiteral("ui_qml")));

        QCOMPARE(uiPackageNames({ modules.path() }),
                 QStringList{ QStringLiteral("web_counter") });
    }

    void lgpmsOwnStagingTreesAreNotPackages()
    {
        // Dot-prefixed siblings of the real install: lgpm's staging and retired
        // trees, either of which can outlive a crash mid-swap holding a valid
        // manifest. package_manager skips them on the same rule.
        QTemporaryDir modules;
        writePackage(modules.path(), QStringLiteral(".staging"),
                     manifest(QStringLiteral("web_counter"), QStringLiteral("ui_qml")));

        QVERIFY(uiPackageNames({ modules.path() }).isEmpty());
    }

    void somethingThatIsNotAPackageIsSkipped()
    {
        QTemporaryDir modules;
        // No manifest at all -- the app's own runtime tree, a leftover
        // directory, the keyring if anyone ever put it here.
        QDir(modules.path()).mkpath(QStringLiteral("qml-runtime"));
        // ...and a manifest that is not JSON. A half-written file is what a
        // crash mid-install leaves, and it must not take the sidebar with it.
        QDir(modules.path()).mkpath(QStringLiteral("torn"));
        QFile torn(QDir(modules.path()).filePath(QStringLiteral("torn/manifest.json")));
        QVERIFY(torn.open(QIODevice::WriteOnly));
        torn.write("{\"name\": \"torn\", \"typ");
        torn.close();

        QVERIFY(uiPackageNames({ modules.path() }).isEmpty());
    }

    void everyDirectoryIsScannedAndANameIsCountedOnce()
    {
        // A Store shell has two: the app's own `web-modules` tree and the
        // writable one an install writes into (ModuleDirectories). A module
        // directory that does not exist is a build that ships no tree, not an
        // error.
        QTemporaryDir shipped;
        QTemporaryDir installed;
        writePackage(shipped.path(), QStringLiteral("web_counter"),
                     manifest(QStringLiteral("web_counter"), QStringLiteral("ui_qml")));
        writePackage(installed.path(), QStringLiteral("notes"),
                     manifest(QStringLiteral("notes"), QStringLiteral("ui_qml")));
        // The same name in both: the user installed a newer copy of something
        // the image ships. One module, one tile.
        writePackage(installed.path(), QStringLiteral("web_counter"),
                     manifest(QStringLiteral("web_counter"), QStringLiteral("ui_qml")));

        QCOMPARE(uiPackageNames({ shipped.path(), installed.path(),
                                 QStringLiteral("/no/such/directory") }),
                 (QStringList{ QStringLiteral("web_counter"), QStringLiteral("notes") }));
    }
};

QTEST_MAIN(InstalledPackagesTest)
#include "installed_packages_test.moc"
