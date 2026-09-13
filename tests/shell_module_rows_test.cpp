// srcdeps: basecamp-shell/src/ShellModuleRows.cpp
//
// WHAT A STORE SHELL'S MODULES TAB AND SIDEBAR SHOW once a module can arrive
// after the build.
//
// Until a phone could install anything, the answer was the Bundled-set manifest
// and nothing else: every row came from it, in its load order, and a row the
// manifest did not account for was a defect worth failing on. A Downloaded
// module breaks that -- it is discovered by the core, in a directory the user's
// install wrote, and the manifest has never heard of it.
//
// So the rule is now two-sided, and BOTH sides matter:
//
//   * the Bundled set is still exactly what it was -- same order, same
//     `embedded` install type, same "a view module reads as loaded when the
//     HOST has it mounted";
//   * everything the core knows that the manifest does not is a DOWNLOADED
//     row, after them, and it is the core's to load, not the host's.
//
// The two are told apart by one thing (the manifest), and this is a pure
// function of the four facts that decide it -- so it is tested here rather than
// on a device, where "the tile is missing" and "the module did not install" look
// identical.
//
// Run: nix build .#unit-tests -L

#include "basecamp-shell/src/ShellModuleRows.h"

#include <QtTest/QtTest>

using basecamp::shell::ModuleFacts;

namespace {

QVariantMap bundled(const QString& name, const QString& type, const QString& version)
{
    QVariantMap m;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("type")] = type;
    m[QStringLiteral("version")] = version;
    return m;
}

QVariantMap rowNamed(const QVariantList& rows, const QString& name)
{
    for (const QVariant& row : rows)
        if (row.toMap().value(QStringLiteral("name")).toString() == name)
            return row.toMap();
    return {};
}

QStringList namesOf(const QVariantList& rows)
{
    QStringList out;
    for (const QVariant& row : rows)
        out << row.toMap().value(QStringLiteral("name")).toString();
    return out;
}

} // namespace

class ShellModuleRowsTest : public QObject {
    Q_OBJECT

private:
    // A phone's Bundled set as this slice builds one, plus one module the user
    // installed from the catalog.
    static ModuleFacts phone()
    {
        ModuleFacts facts;
        facts.bundledSet = {
            bundled(QStringLiteral("capability_module"), QStringLiteral("core"), QStringLiteral("1.0.0")),
            bundled(QStringLiteral("package_manager"), QStringLiteral("core"), QStringLiteral("1.2.0")),
            bundled(QStringLiteral("chat_ui"), QStringLiteral("ui_qml"), QStringLiteral("0.9.0")),
        };
        // package_manager is in the manifest and NOT in `known`: its image is
        // wrong, which is the one thing a Bundled row can say is broken.
        facts.known = { QStringLiteral("capability_module") };
        facts.loaded = { QStringLiteral("capability_module") };
        return facts;
    }

private slots:
    // ── the Bundled set is unchanged ──────────────────────────────────────

    void theBundledSetIsStillEveryRowInTheManifestsOrder()
    {
        const QVariantList rows = basecamp::shell::moduleRows(phone());
        QCOMPARE(namesOf(rows),
                 (QStringList{ QStringLiteral("capability_module"),
                               QStringLiteral("package_manager"),
                               QStringLiteral("chat_ui") }));
        for (const QVariant& row : rows)
            QCOMPARE(row.toMap().value(QStringLiteral("installType")).toString(),
                     QStringLiteral("embedded"));
    }

    void aBundledViewModuleReadsAsLoadedOnlyWhenTheHostMountedIt()
    {
        ModuleFacts facts = phone();
        QVERIFY(!rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("chat_ui"))
                     .value(QStringLiteral("isLoaded")).toBool());

        facts.mountedViews.insert(QStringLiteral("chat_ui"));
        QVERIFY(rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("chat_ui"))
                    .value(QStringLiteral("isLoaded")).toBool());
    }

    void aBundledMemberTheCoreNeverRegisteredSaysSo()
    {
        // package_manager is in the manifest and not in `known`: its image is
        // wrong, and the closure was resolved at BUILD time so there is no
        // missing dependency to install.
        const QVariantList rows = basecamp::shell::moduleRows(phone());
        QVERIFY(rowNamed(rows, QStringLiteral("package_manager"))
                    .value(QStringLiteral("hasMissingDeps")).toBool());
        QVERIFY(!rowNamed(rows, QStringLiteral("capability_module"))
                     .value(QStringLiteral("hasMissingDeps")).toBool());
    }

    // ── the Downloaded module ─────────────────────────────────────────────

    void aModuleTheManifestDoesNotAccountForIsDownloaded()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");

        QCOMPARE(basecamp::shell::downloadedModules(facts),
                 QStringList{ QStringLiteral("web_counter") });

        const QVariantMap row =
            rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("web_counter"));
        QCOMPARE(row.value(QStringLiteral("installType")).toString(),
                 QStringLiteral("downloaded"));
        QVERIFY(row.value(QStringLiteral("isLoaded")).toBool());
        // Nothing is wrong with it: `known` is how it got here.
        QVERIFY(!row.value(QStringLiteral("hasMissingDeps")).toBool());
    }

    void aDownloadedRowComesAfterTheWholeBundledSet()
    {
        // The manifest's order is the closure's LOAD order, and a row spliced
        // into it by name would read as part of that closure.
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("aaa_downloaded");
        QCOMPARE(namesOf(basecamp::shell::moduleRows(facts)).last(),
                 QStringLiteral("aaa_downloaded"));
    }

    void aBundledMemberIsNeverAlsoADownloadedOne()
    {
        // Every Bundled member the core registered is in `known` too; counting
        // it twice would put a second row under the same name.
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("chat_ui");
        QVERIFY(basecamp::shell::downloadedModules(facts).isEmpty());
        QCOMPARE(basecamp::shell::moduleRows(facts).size(), 3);
    }

    // ── the sidebar ───────────────────────────────────────────────────────

    void theSidebarCarriesTheBundledSetsViewModules()
    {
        QCOMPARE(namesOf(basecamp::shell::launcherApps(phone())),
                 QStringList{ QStringLiteral("chat_ui") });
    }

    void aDownloadedModuleWithAPageIsAnApp()
    {
        // What makes a Downloaded module an APP is that the Web container
        // opened a page for it. The Shell cannot read that off a manifest it
        // does not have, and it must not guess from the name: a `web` variant
        // that is a headless module has no UI to mount and no tile to press.
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter") << QStringLiteral("web_indexer");
        facts.loaded << QStringLiteral("web_counter") << QStringLiteral("web_indexer");
        facts.openPages.insert(QStringLiteral("web_counter"));

        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 (QStringList{ QStringLiteral("chat_ui"), QStringLiteral("web_counter") }));
    }

    void aDownloadedAppsRowSaysItIsAView()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");
        facts.openPages.insert(QStringLiteral("web_counter"));

        QCOMPARE(rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("web_counter"))
                     .value(QStringLiteral("type")).toString(),
                 QStringLiteral("ui_qml"));
    }

    void aDownloadedTileIsLoadedWhileItsPageIsOpen()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");
        facts.openPages.insert(QStringLiteral("web_counter"));

        const QVariantMap tile =
            rowNamed(basecamp::shell::launcherApps(facts), QStringLiteral("web_counter"));
        QVERIFY(tile.value(QStringLiteral("isLoaded")).toBool());
        // A Downloaded package can be missing a dependency -- unlike a Bundled
        // one, whose closure was resolved and verified at build time -- but the
        // core discovered and loaded this one, so nothing is blocked.
        QVERIFY(!tile.value(QStringLiteral("hasMissingDeps")).toBool());
    }

    // ── the app's own `web-modules` tree ──────────────────────────────────
    //
    // A phone app ships two kinds of module and the Bundled-set manifest names
    // only one of them. The manifest is the native Bare frameworks; the app
    // ALSO carries a `web-modules` directory, which the core discovers exactly
    // as it discovers an installed package -- same scan, same shape. Told apart
    // by nothing, a shipped module reads as something the build downloaded for
    // the user, which is a claim about where code came from.

    void aShippedWebModuleIsEmbeddedAndNotDownloaded()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");

        QVERIFY(basecamp::shell::downloadedModules(facts).isEmpty());
        const QVariantMap row =
            rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("web_counter"));
        QCOMPARE(row.value(QStringLiteral("installType")).toString(),
                 QStringLiteral("embedded"));
        QVERIFY(row.value(QStringLiteral("isLoaded")).toBool());
    }

    void aShippedAndAnInstalledModuleAreBothRowsAndOnlyOneIsDownloaded()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter") << QStringLiteral("notes");

        QCOMPARE(basecamp::shell::downloadedModules(facts),
                 QStringList{ QStringLiteral("notes") });
        const QStringList names = namesOf(basecamp::shell::moduleRows(facts));
        QCOMPARE(names.size(), 5);
        // The manifest first, then what the build shipped beside it, then what
        // the user installed.
        QCOMPARE(names.mid(3),
                 (QStringList{ QStringLiteral("web_counter"), QStringLiteral("notes") }));
    }

    void aShippedWebModuleWithAPageIsAnAppToo()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");
        facts.openPages.insert(QStringLiteral("web_counter"));

        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 (QStringList{ QStringLiteral("chat_ui"), QStringLiteral("web_counter") }));
    }

    // ── the tile and the mount are the same question ──────────────────────
    //
    // The sidebar draws a tile, and pressing it sends the name to
    // BundledSetShellHost::mountApp, which has to decide whether to dock a
    // QQuickWidget or tell the Web container to bring a page forward. If the
    // tile is made by one rule and the mount gated by another, a tile can exist
    // for a module the host then refuses -- a button that does nothing, on a
    // phone, where there is nothing else to press.

    void everyTileTheSidebarDrawsIsOneTheHostCanMount()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_shipped") };
        facts.known << QStringLiteral("web_shipped") << QStringLiteral("web_installed");
        facts.loaded << QStringLiteral("web_shipped") << QStringLiteral("web_installed");
        facts.openPages.insert(QStringLiteral("web_shipped"));
        facts.openPages.insert(QStringLiteral("web_installed"));
        facts.mountedViews.insert(QStringLiteral("chat_ui"));

        for (const QString& name : namesOf(basecamp::shell::launcherApps(facts))) {
            // chat_ui is the Bundled view module: the HOST instantiates it, so
            // it is the one tile that is NOT the container's.
            const bool isBundledView = name == QLatin1String("chat_ui");
            QCOMPARE(basecamp::shell::isWebContainerApp(facts, name), !isBundledView);
        }
    }

    void aModuleWithNoPageIsNotTheContainersToShow()
    {
        // Nothing to bring forward: a `web` variant of a headless module opens
        // no page, and a Bundled framework never had one.
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_indexer") };
        facts.known << QStringLiteral("web_indexer") << QStringLiteral("notes");

        QVERIFY(!basecamp::shell::isWebContainerApp(facts, QStringLiteral("web_indexer")));
        QVERIFY(!basecamp::shell::isWebContainerApp(facts, QStringLiteral("notes")));
        QVERIFY(!basecamp::shell::isWebContainerApp(facts, QStringLiteral("chat_ui")));
    }
};

QTEST_MAIN(ShellModuleRowsTest)
#include "shell_module_rows_test.moc"
