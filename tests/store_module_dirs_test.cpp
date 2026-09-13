// srcdeps: appmanager/ModuleDirectories.cpp
//
// THE ONE DIRECTORY A DOWNLOADED MODULE HAS TO LAND IN (slice 29).
//
// An install that succeeds and then does nothing is the failure this holds
// shut. package_manager writes where the Shell tells it; the core discovers
// modules only in the directories it was handed before it started; the Web
// container serves whatever the core found. Point the first somewhere the
// second does not scan and every step reports success while the module never
// appears -- which is exactly what the Shell did, handing package_manager
// `<appData>/modules` while the core scanned `<appData>/logos/modules`.
//
// Run: nix build .#unit-tests -L
#include "appmanager/ModuleDirectories.h"

#include <QFileInfo>
#include <QtTest/QtTest>

using basecamp::appmanager::ModuleDirectories;

class StoreModuleDirsTest : public QObject
{
    Q_OBJECT

private slots:
    // THE INVARIANT. Everything else in this file is detail.
    void installDirectoryIsOneTheCoreScans()
    {
        const ModuleDirectories dirs = ModuleDirectories::under(
            QStringLiteral("/data/app"), QStringLiteral("/bundle/web-modules"));
        QVERIFY2(dirs.coreModulesDirs.contains(dirs.installModulesDir),
                 qPrintable(QStringLiteral("install dir %1 is not scanned; core scans %2")
                                .arg(dirs.installModulesDir,
                                     dirs.coreModulesDirs.join(QLatin1Char(':')))));
    }

    // ...and it still holds for the build that ships no `web` tree at all,
    // which is the common Store shell.
    void invariantHoldsWithNoShippedWebModules()
    {
        const ModuleDirectories dirs = ModuleDirectories::under(QStringLiteral("/data/app"));
        QVERIFY(dirs.coreModulesDirs.contains(dirs.installModulesDir));
        // One directory, not two -- an empty shipped tree must not reach the
        // core as an empty string, which it would scan as the working directory.
        QCOMPARE(dirs.coreModulesDirs.size(), 1);
        QVERIFY(!dirs.coreModulesDirs.contains(QString()));
    }

    void shippedWebModulesAreScannedToo()
    {
        const ModuleDirectories dirs = ModuleDirectories::under(
            QStringLiteral("/data/app"), QStringLiteral("/bundle/web-modules"));
        QVERIFY(dirs.coreModulesDirs.contains(QStringLiteral("/bundle/web-modules")));
        QCOMPARE(dirs.coreModulesDirs.size(), 2);
        // The build-time tree first, the user's last.
        QCOMPARE(dirs.coreModulesDirs.first(), QStringLiteral("/bundle/web-modules"));
    }

    // The UI half goes beside the module half and NOT into it: lgpm treats the
    // two as separate destinations, and nesting one inside the other would make
    // the core's scan of the modules directory walk into a tree of QML.
    void uiPluginsAreBesideModulesNotInsideThem()
    {
        const ModuleDirectories dirs = ModuleDirectories::under(QStringLiteral("/data/app"));
        QVERIFY(!dirs.installUiPluginsDir.startsWith(dirs.installModulesDir));
        QCOMPARE(QFileInfo(dirs.installUiPluginsDir).path(),
                 QFileInfo(dirs.installModulesDir).path());
    }

    // Everything is under the app sandbox. A phone has no shared modules
    // directory (ADR 0003), so a path that escaped the root would be a path the
    // app cannot write.
    void everythingStaysUnderTheAppDataRoot()
    {
        const ModuleDirectories dirs = ModuleDirectories::under(QStringLiteral("/data/app"));
        for (const QString& dir : dirs.coreModulesDirs)
            QVERIFY(dir.startsWith(QStringLiteral("/data/app/")));
        QVERIFY(dirs.installUiPluginsDir.startsWith(QStringLiteral("/data/app/")));
    }
};

QTEST_MAIN(StoreModuleDirsTest)
#include "store_module_dirs_test.moc"
