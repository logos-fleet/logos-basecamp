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

    // ── a module that is installed and NOT running (#123) ─────────────────
    //
    // A Downloaded module is loaded exactly once: by the install that brought
    // it. On every LATER launch the core discovers it in the scanned directory
    // and waits to be asked, and a Store shell's cold start deliberately does
    // not ask -- a `web` module's page is 290 MB of QML runtime and seconds of
    // it. So on the second launch the module has no page, and a rule that made
    // the page the only evidence of a UI took the app off the sidebar: it was
    // in the Modules tab, and where the user left it there was nothing.
    //
    // The package's own manifest is the evidence that does not need a page. It
    // is on disk from the install, and its `type` is the SAME field the
    // container reads to decide whether a page serves a UI
    // (MobileWebModuleView's servesUiOf), so the two cannot disagree.

    void anInstalledAppHasATileBeforeAnythingHasLoadedIt()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        // Not loaded, no page: this is a second launch.
        facts.uiPackages.insert(QStringLiteral("web_counter"));

        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 (QStringList{ QStringLiteral("chat_ui"), QStringLiteral("web_counter") }));
        const QVariantMap tile =
            rowNamed(basecamp::shell::launcherApps(facts), QStringLiteral("web_counter"));
        // ...and it says so: the tile is there, the module is not running, and
        // pressing it is what brings it up.
        QVERIFY(!tile.value(QStringLiteral("isLoaded")).toBool());
    }

    void thatTileIsOneTheHostMountsThroughTheContainer()
    {
        // The tile and the mount are one question (see below): a tile the host
        // would refuse is a button that does nothing.
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        facts.uiPackages.insert(QStringLiteral("web_counter"));

        QVERIFY(basecamp::shell::isWebContainerApp(facts, QStringLiteral("web_counter")));
    }

    void anInstalledCoreModuleStillHasNoTile()
    {
        // A `core` module's `web` variant has a page too -- a wasm image needs
        // a document -- and no user interface. Its manifest says `core`, so it
        // is not in this set and never gets a tile.
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_indexer");
        facts.loaded << QStringLiteral("web_indexer");

        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 QStringList{ QStringLiteral("chat_ui") });
        QVERIFY(!basecamp::shell::isWebContainerApp(facts, QStringLiteral("web_indexer")));
    }

    void anInstalledAppsRowSaysItIsAViewBeforeItRuns()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("web_counter");
        facts.uiPackages.insert(QStringLiteral("web_counter"));

        const QVariantMap row =
            rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("web_counter"));
        QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("ui_qml"));
        QVERIFY(!row.value(QStringLiteral("isLoaded")).toBool());
    }

    void aShippedAppHasATileBeforeAnythingHasLoadedItEither()
    {
        // The app's own `web-modules` tree is not loaded at startup either, and
        // it is the same rule: the manifest is beside the module in the image.
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter");
        facts.uiPackages.insert(QStringLiteral("web_counter"));

        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 (QStringList{ QStringLiteral("chat_ui"), QStringLiteral("web_counter") }));
    }

    void aBundledViewModuleIsNeverTheContainersEvenIfItsPackageDeclaresAUi()
    {
        // A `ui_qml` member of the manifest is the HOST's to instantiate (ADR
        // 0006). Its package declares a UI like any other, and a manifest entry
        // is what keeps that from turning it into a page.
        ModuleFacts facts = phone();
        facts.uiPackages.insert(QStringLiteral("chat_ui"));
        facts.known << QStringLiteral("chat_ui");

        QVERIFY(!basecamp::shell::isWebContainerApp(facts, QStringLiteral("chat_ui")));
        QCOMPARE(namesOf(basecamp::shell::launcherApps(facts)),
                 QStringList{ QStringLiteral("chat_ui") });
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

    // ── WHOSE Load/Unload button is it (#149) ────────────────────────────
    //
    // A `web` app's row is a `ui_qml` row -- it has a user interface and the
    // sidebar carries a tile for it -- and its MODULE is the core's: the page
    // lives in the Web container, and the container is a core container. A
    // Bundled `ui_qml` member is the opposite: same type, and the host
    // instantiates the framework in this process, so the core has no handle on
    // it to unload (ADR 0006).
    //
    // Everything that asked this question by reading the TYPE therefore got a
    // `web` app wrong. The Modules tab drew it an enabled Unload button, the
    // acceptance driver skipped its row as "a view module's toggle is a no-op
    // by design", and neither was talking about the same modules as the
    // backend's own refusal, which reads the manifest. Hence one fact on the
    // row, and one function behind it.

    void aBundledViewModulesRowIsTheHostsToLoad()
    {
        const ModuleFacts facts = phone();
        QVERIFY(rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("chat_ui"))
                    .value(QStringLiteral("hostLoaded")).toBool());
        QVERIFY(basecamp::shell::hostLoadedModule(facts, QStringLiteral("chat_ui")));
    }

    void aBundledCoreModulesRowIsTheCoresToLoad()
    {
        const ModuleFacts facts = phone();
        QVERIFY(!rowNamed(basecamp::shell::moduleRows(facts),
                          QStringLiteral("capability_module"))
                     .value(QStringLiteral("hostLoaded")).toBool());
        QVERIFY(!basecamp::shell::hostLoadedModule(facts,
                                                   QStringLiteral("capability_module")));
    }

    void aShippedWebAppsRowSaysItIsAViewAndStillTheCoresToLoad()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");
        facts.openPages.insert(QStringLiteral("web_counter"));

        const QVariantMap row =
            rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("web_counter"));
        // It IS a view -- the sidebar gives it a tile on this same rule...
        QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("ui_qml"));
        // ...and the Unload button on its row reaches the core all the same.
        QVERIFY(!row.value(QStringLiteral("hostLoaded")).toBool());
        QVERIFY(!basecamp::shell::hostLoadedModule(facts, QStringLiteral("web_counter")));
    }

    void anInstalledWebAppsRowIsTheCoresToLoadBeforeItEvenRuns()
    {
        ModuleFacts facts = phone();
        facts.known << QStringLiteral("notes");
        facts.uiPackages.insert(QStringLiteral("notes"));

        const QVariantMap row =
            rowNamed(basecamp::shell::moduleRows(facts), QStringLiteral("notes"));
        QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("ui_qml"));
        QVERIFY(!row.value(QStringLiteral("hostLoaded")).toBool());
        QVERIFY(!basecamp::shell::hostLoadedModule(facts, QStringLiteral("notes")));
    }

    // ── the Apps Inspector (#146) ────────────────────────────────────────
    //
    // TWO PANES, TWO QUESTIONS. Settings has a Module Inspector titled "Core
    // modules known to the runtime" and an Apps Inspector titled "UI plugins
    // available in this installation", and on a phone only the first of them
    // had anything behind it: the Shell's `uiModulesModel` was null, because
    // a Store shell has no UI-plugin directory to scan (ADR 0003).
    //
    // So a `web` app was reported as listed under Modules and nowhere else,
    // while the same Shell drew it a sidebar tile and mounted it in the dock.
    // The Modules pane is not wrong -- it is "everything the core knows", and
    // that is what its title says -- the Apps pane was EMPTY.
    //
    // The set it shows is the set the SIDEBAR shows, derived from the very
    // same rule: a row here for every tile and a tile for every row, or the
    // two panes disagree again about what an app is. And each row is that
    // module's Modules-tab row, unchanged -- the same version, the same
    // installType, the same `hostLoaded` -- so the Load/Unload button in
    // either pane is the same button.

    void theAppsInspectorListsTheBundledSetsViewModules()
    {
        QCOMPARE(namesOf(basecamp::shell::appRows(phone())),
                 QStringList{ QStringLiteral("chat_ui") });
    }

    void aWebAppIsAnAppsInspectorRowLikeAnyOtherApp()
    {
        // wallet_ui as the phone carries it: a `web` variant the app image
        // ships, whose package declares a UI and which nothing has loaded yet.
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("wallet_ui") };
        facts.known << QStringLiteral("wallet_ui");
        facts.uiPackages.insert(QStringLiteral("wallet_ui"));

        QCOMPARE(namesOf(basecamp::shell::appRows(facts)),
                 (QStringList{ QStringLiteral("chat_ui"), QStringLiteral("wallet_ui") }));
    }

    void aCoreModuleIsNeverAnAppsInspectorRow()
    {
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_indexer") };
        facts.known << QStringLiteral("web_indexer") << QStringLiteral("notes");

        const QStringList apps = namesOf(basecamp::shell::appRows(facts));
        // Neither the Bundled core modules, nor a `web` variant of a headless
        // one, nor an installed module whose package declares no UI.
        QVERIFY(!apps.contains(QStringLiteral("capability_module")));
        QVERIFY(!apps.contains(QStringLiteral("package_manager")));
        QVERIFY(!apps.contains(QStringLiteral("web_indexer")));
        QVERIFY(!apps.contains(QStringLiteral("notes")));
    }

    void theAppsPaneAndTheSidebarShowTheSameSet()
    {
        // The claim the two panes could not make before: an app the Shell
        // draws a tile for is an app the Apps Inspector lists, and nothing
        // else is.
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("wallet_ui") };
        facts.known << QStringLiteral("wallet_ui") << QStringLiteral("notes")
                    << QStringLiteral("web_counter");
        facts.loaded << QStringLiteral("web_counter");
        facts.uiPackages.insert(QStringLiteral("wallet_ui"));
        facts.openPages.insert(QStringLiteral("web_counter"));
        facts.mountedViews.insert(QStringLiteral("chat_ui"));

        QStringList apps = namesOf(basecamp::shell::appRows(facts));
        QStringList tiles = namesOf(basecamp::shell::launcherApps(facts));
        apps.sort();
        tiles.sort();
        QCOMPARE(apps, tiles);
    }

    void anAppsInspectorRowIsThatModulesRow()
    {
        // Same row, out of the same builder: whatever the Modules tab says
        // about an app, the Apps pane says too. A second shape here would be
        // a second answer to "is it loaded" and to "whose button is this".
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("wallet_ui") };
        facts.known << QStringLiteral("wallet_ui");
        facts.loaded << QStringLiteral("wallet_ui");
        facts.openPages.insert(QStringLiteral("wallet_ui"));
        facts.mountedViews.insert(QStringLiteral("chat_ui"));

        const QVariantList rows = basecamp::shell::moduleRows(facts);
        for (const QVariant& value : basecamp::shell::appRows(facts)) {
            const QVariantMap app = value.toMap();
            QCOMPARE(app, rowNamed(rows, app.value(QStringLiteral("name")).toString()));
        }
    }

    void aWebAppsRowInTheAppsPaneIsStillTheCoresToLoad()
    {
        // #149, restated for the pane that did not exist then: the Apps
        // Inspector's toggle is the same button, so it must carry the same
        // answer to whose module it is.
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("wallet_ui") };
        facts.known << QStringLiteral("wallet_ui");
        facts.uiPackages.insert(QStringLiteral("wallet_ui"));

        const QVariantList apps = basecamp::shell::appRows(facts);
        QVERIFY(rowNamed(apps, QStringLiteral("chat_ui"))
                    .value(QStringLiteral("hostLoaded")).toBool());
        QVERIFY(!rowNamed(apps, QStringLiteral("wallet_ui"))
                     .value(QStringLiteral("hostLoaded")).toBool());
    }

    void everyRowTheHostOwnsIsOneTheSidebarMountsItself()
    {
        // The two rules that split the same set have to split it the SAME way,
        // or a module is both the host's to mount and the core's to unload.
        // Stated over every row rather than over one, because the set a build
        // ships is data (ADR 0007).
        ModuleFacts facts = phone();
        facts.shipped = { QStringLiteral("web_counter") };
        facts.known << QStringLiteral("web_counter") << QStringLiteral("notes");
        facts.uiPackages.insert(QStringLiteral("web_counter"));
        facts.uiPackages.insert(QStringLiteral("notes"));

        for (const QVariant& value : basecamp::shell::moduleRows(facts)) {
            const QVariantMap row = value.toMap();
            const QString name = row.value(QStringLiteral("name")).toString();
            const bool hostLoaded = row.value(QStringLiteral("hostLoaded")).toBool();
            QCOMPARE(hostLoaded, basecamp::shell::hostLoadedModule(facts, name));
            // A module the host owns is never one the container shows, and
            // the other way round.
            QVERIFY(!(hostLoaded && basecamp::shell::isWebContainerApp(facts, name)));
        }
    }
};

QTEST_MAIN(ShellModuleRowsTest)
#include "shell_module_rows_test.moc"
