#include "ShellModulesDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"
#include "webview/MobileWebContainerBackend.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QQuickItem>
#include <QVariant>

ShellModulesDriver::ShellModulesDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                       QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

// WHAT AN UNLOADED `web` APP LEAVES BEHIND, checked between the two presses.
//
// The Modules tab's Unload is the core's, and for an app whose UI is a page it
// has three consequences outside the core -- one in the container, one in the
// Shell's own docks, one on the sidebar. The row going to "Not loaded" says
// nothing about any of them, and the sidebar tile surviving an unload was half
// of what #149 reported.
void ShellModulesDriver::checkUnloadedWebApp(const QString& name)
{
    auto* web = basecamp::web::MobileWebContainerBackend::instance();
    if (web->hasView(name)) {
        emit log(QStringLiteral("WRONG: %1 is unloaded and the container still has its "
                                "page").arg(name));
        return;
    }
    // AND THE BOOKS SAY THE SAME THING AS THE PAGES (#151). The budget is what
    // decides which app may have a QML runtime alive -- ONE of them on a phone
    // -- so a module with no page left in the live set is 290 MB of budget
    // spent on nothing, and the next app the user opens has to evict a dead
    // one to get it back. It is also the half the console disagreed with
    // itself over: `web_counter is visible; 1 live runtime(s)` from the
    // container, `app web_counter is not mounted` from the Shell.
    if (web->budget().isLive(name) || web->budget().visible() == name
        || web->frontmostModule() == name) {
        emit log(QStringLiteral("WRONG: %1 is unloaded and the container still counts it "
                                "-- live: %2, visible: '%3', in front: '%4'")
                     .arg(name, web->budget().live().join(QStringLiteral(", ")),
                          web->budget().visible(), web->frontmostModule()));
        return;
    }
    if (m_host->webSurface(name)) {
        emit log(QStringLiteral("WRONG: %1 is unloaded and the Shell still holds a tab "
                                "onto its page").arg(name));
        return;
    }
    // AND THE TILE STAYS, which is not the same claim. The app is installed
    // whether or not it is running, so the sidebar carries it and the press is
    // what loads it (#123) -- but it must not go on claiming to be up.
    QVariantMap tile;
    for (const QVariant& value : m_host->backend()->launcherApps()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("name")).toString() == name) {
            tile = candidate;
            break;
        }
    }
    if (tile.isEmpty()) {
        emit log(QStringLiteral("WRONG: unloading %1 took its tile off the sidebar -- the "
                                "app is still installed and pressing it is what loads it")
                     .arg(name));
        return;
    }
    if (tile.value(QStringLiteral("isLoaded")).toBool()) {
        emit log(QStringLiteral("WRONG: %1 is unloaded and its sidebar tile still reads "
                                "as running").arg(name));
        return;
    }
    emit log(QStringLiteral("drive modules: %1 unloaded -- no page, no tab, %2 live "
                            "runtime(s) held, and its tile is on the sidebar and not "
                            "claiming to be up")
                 .arg(name).arg(web->budget().live().size()));
}

// WHICH PANE LISTS AN APP (#146).
//
// Settings has a Module Inspector -- "Core modules known to the runtime", which
// is every module the core has, apps included -- and an Apps Inspector, "UI
// plugins available in this installation". On a phone the second had nothing
// behind it, so a `web` app appeared under Modules only, and a user looking for
// the app they can see a tile for found it filed as a core module.
//
// The tiles are the answer this compares against, because they are what the
// Shell ALREADY acts on: a tile is drawn, pressed and mounted, so a pane that
// lists a different set is the pane that is wrong.
void ShellModulesDriver::checkAppsInspector()
{
    ShellModulesBackend* backend = m_host->backend();

    m_host->setCurrentSectionIndex(ShellSection::Settings);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QQuickItem* section = waitFor(QStringLiteral("settings.section.apps_inspector"), 5000);
    if (!section) {
        dumpNames(QStringLiteral("no Apps Inspector section in the Settings view"));
        return;
    }
    scrollIntoView(section);
    if (!tap(section)) return;

    QQuickItem* view = waitFor(QStringLiteral("appsInspectorView"), 5000);
    if (!view || !view->isVisible()) {
        emit log(QStringLiteral("drive: the Apps Inspector did not come to the front"));
        return;
    }
    emit log(QStringLiteral("shell: Settings -> Apps Inspector is on screen"));

    QStringList apps;
    for (const QVariant& value : backend->launcherApps())
        apps << value.toMap().value(QStringLiteral("name")).toString();
    apps.sort();

    QStringList rows;
    {
        const QString prefix = QStringLiteral("appsInspector.status.");
        // One delegate at a time, as the Modules tab does: cacheBuffer keeps
        // every row instantiated but not necessarily by the tick the view
        // appeared in.
        waitFor(prefix + apps.value(0), 5000);
        forEachItem([&rows, &prefix](QQuickItem* item) {
            if (item->objectName().startsWith(prefix))
                rows << item->objectName().mid(prefix.size());
        });
        rows.removeDuplicates();
        rows.sort();
    }
    emit log(QStringLiteral("apps tab rows:   %1")
                 .arg(rows.isEmpty() ? QStringLiteral("(none)")
                                     : rows.join(QStringLiteral(", "))));
    emit log(QStringLiteral("sidebar tiles:   %1")
                 .arg(apps.isEmpty() ? QStringLiteral("(none)")
                                     : apps.join(QStringLiteral(", "))));
    if (rows != apps) {
        emit log(QStringLiteral("WRONG: the Apps Inspector does not list the apps the "
                                "Shell carries tiles for"));
        return;
    }
    if (apps.isEmpty()) {
        // Legitimate: `--bundle capability_module,chat_module` is a set with no
        // app in it, and an empty pane is the right answer for one.
        emit log(QStringLiteral("SHELL APPS TAB LISTS THE APPS (none: this set has no app)"));
        return;
    }

    // ...AND UNDER MODULES TOO, which is the other half of what #146 settled.
    // The Modules pane is titled "Core modules known to the runtime" and shows
    // exactly that; an app IS a module the core knows, so it belongs in both
    // lists and the defect was only ever the one that was empty. Asserted
    // rather than assumed, because "fix the Apps pane" and "move the apps out
    // of the Modules pane" are the two readings of the report and this is the
    // one that was chosen.
    QStringList missingFromModules;
    for (const QString& name : apps) {
        if (!backend->shippedModuleNames().contains(name)
            && !backend->downloadedModules().contains(name))
            missingFromModules << name;
    }
    if (!missingFromModules.isEmpty()) {
        emit log(QStringLiteral("WRONG: app(s) the Modules pane does not account for: %1")
                     .arg(missingFromModules.join(QStringLiteral(", "))));
        return;
    }
    emit log(QStringLiteral("SHELL APPS TAB LISTS THE APPS (%1), EACH ALSO KNOWN TO THE "
                            "RUNTIME").arg(apps.join(QStringLiteral(", "))));
}

void ShellModulesDriver::run()
{
    ShellModulesBackend* backend = m_host->backend();

    // ── 0. the OTHER inspector, while nothing has moved yet ──
    checkAppsInspector();

    // ── 1. open Settings -> Module Inspector ──
    // The top-level section is the HOST's to set: that is what IShellHost's
    // setCurrentSectionIndex is, and a sidebar tap would only be a longer way
    // to reach it. The sub-section is the Shell's own, so it is tapped.
    m_host->setCurrentSectionIndex(ShellSection::Settings);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QQuickItem* section = waitFor(QStringLiteral("settings.section.module_inspector"), 5000);
    if (!section) {
        dumpNames(QStringLiteral("no Module Inspector section in the Settings view"));
        return;
    }
    // The strip a phone draws the sections in scrolls; swipe it first.
    scrollIntoView(section);
    if (!tap(section)) return;

    QQuickItem* view = waitFor(QStringLiteral("moduleInspectorView"), 5000);
    if (!view || !view->isVisible()) {
        emit log(QStringLiteral("drive: the Module Inspector did not come to the front"));
        return;
    }
    emit log(QStringLiteral("shell: Settings -> Module Inspector is on screen"));

    // ── 2. the rows on screen ARE what the app SHIPS ──
    // Counted off the SCENE, not off the model: the model is built from the
    // manifest, so the two agreeing would prove only that this host can copy
    // a list. What can still go wrong above it is the view -- a filter proxy
    // dropping a row, a delegate that never instantiated, a table showing a
    // module the manifest does not account for -- and each row's status badge
    // carries its module's name, so the badges ARE the rendered list.
    // The Bundled-set manifest PLUS the app's own `web-modules` tree. Both came
    // in with the app image; the manifest names only the native Bare
    // frameworks, and a shell that asserted on it alone would fail on its own
    // shipped `web` modules the moment it carried a Web container.
    const QStringList set = backend->shippedModuleNames();
    QStringList rows;
    {
        const QString prefix = QStringLiteral("moduleInspector.status.");
        // One delegate at a time: the table keeps every row instantiated
        // (cacheBuffer), but not necessarily by the tick the view appeared in.
        waitFor(prefix + set.value(0), 5000);
        forEachItem([&rows, &prefix](QQuickItem* item) {
            if (item->objectName().startsWith(prefix))
                rows << item->objectName().mid(prefix.size());
        });
        rows.removeDuplicates();
        rows.sort();
    }
    QStringList expected = set;
    expected.sort();
    emit log(QStringLiteral("modules tab rows: %1")
                 .arg(rows.isEmpty() ? QStringLiteral("(none)")
                                     : rows.join(QStringLiteral(", "))));
    emit log(QStringLiteral("app ships:        %1").arg(expected.join(QStringLiteral(", "))));
    QStringList downloaded = backend->downloadedModules();
    downloaded.sort();
    if (!downloaded.isEmpty()) {
        // Legitimate, and it has to be SAID: a Store shell that installed
        // something is the point of the App Manager, and a row the app image
        // does not account for is only a defect when nothing installed one.
        emit log(QStringLiteral("downloaded:       %1")
                     .arg(downloaded.join(QStringLiteral(", "))));
        expected += downloaded;
        expected.sort();
    }
    if (rows != expected) {
        emit log(QStringLiteral("WRONG: the Modules tab does not list what the app has"));
        return;
    }
    emit log(QStringLiteral("SHELL MODULES TAB LISTS WHAT THE APP HAS"));

    // Every check below reads a row off its own status badge rather than off
    // the backend: what the user sees is the claim, and a model that moved
    // without the view following is the failure they are looking for. Re-found
    // on each reading, because a delegate is free to be rebuilt under us.
    const auto modelRow = [this](const QString& name) -> QObject* {
        QQuickItem* badge = find(QStringLiteral("moduleInspector.status.%1").arg(name));
        return badge ? badge->property("row").value<QObject*>() : nullptr;
    };
    // WHOSE MODULE THIS ROW IS. A Bundled `ui_qml` member is mounted by the
    // host rather than run by the core (ADR 0006), so its row says nothing
    // about the Native container.
    //
    // OFF THE ROW'S OWN `hostLoaded`, not off its type, and that is #149: a
    // `web` app's row is a `ui_qml` row whose module the CORE owns -- its page
    // lives in the Web container -- so reading the type here skipped exactly
    // the rows whose Unload the user was complaining about, and called the
    // skip a design decision on the way past.
    const auto isHostRow = [](QObject* row) {
        return row && row->property("hostLoaded").toBool();
    };
    const auto isLoaded = [&modelRow](const QString& name) {
        QObject* row = modelRow(name);
        return row && row->property("isLoaded").toBool();
    };

    // ── 2b. every row came in with the app image ──
    // A Store shell may not gain a native module at runtime (ADR 0003), so
    // "the app contains no Downloaded module" is not a thing to hope for --
    // it is a thing the tab must be able to SAY. `installType` is the column
    // Basecamp answers that with everywhere else, and "embedded" is its
    // answer for something the build put there. Anything else in this table
    // would be a module that arrived some other way.
    QStringList notEmbedded;
    for (const QString& name : rows) {
        // ...except what the user installed, which is `downloaded` BY DESIGN
        // and is the one thing this column exists to tell apart.
        if (downloaded.contains(name))
            continue;
        QObject* row = modelRow(name);
        const QString installType = row ? row->property("installType").toString()
                                        : QStringLiteral("<no row>");
        if (installType != QLatin1String("embedded"))
            notEmbedded << QStringLiteral("%1 (%2)").arg(name, installType);
    }
    if (!notEmbedded.isEmpty()) {
        emit log(QStringLiteral("WRONG: not every row is embedded: %1")
                     .arg(notEmbedded.join(QStringLiteral(", "))));
        return;
    }
    emit log(QStringLiteral("all %1 shipped row(s) are installType 'embedded'; %2 downloaded")
                 .arg(rows.size() - downloaded.size()).arg(downloaded.size()));

    // ── 2c. and the tab shows their stats ──
    // Off the rendered CELLS, for the same reason the row list is counted off
    // the scene: the model carries cpu and memory whether or not the table
    // ever drew them, and an unloaded row deliberately renders an em dash. So
    // a loaded row must show a figure, and it is the figure on screen that
    // has to be one.
    //
    // TWO LAYOUTS, because the table has two (logos-workspace#84): wider than
    // what the desktop columns ask for, CPU and memory are their own cells;
    // narrower -- which every phone is, and a 13-inch iPad's Settings pane
    // too -- the two fold into one line inside the module cell. Asking only
    // for the desktop pair would report "no stats cells" for a table that is
    // showing its stats perfectly well.
    //
    // WHICH ROWS OWE A FIGURE, which used to be "at least one of them" and is
    // now stated per row -- both halves of that were wrong.
    //
    // A set whose members all start UNLOADED owes none: every row renders the
    // em dash it is supposed to, and demanding one anyway aborted this drive
    // before step 3, so such a run silently never pressed a toggle
    // (logos-workspace#86).
    //
    // And "at least one" was too weak for what #86 was actually about. Every
    // Bundled module is loaded into the host's own process, so all of them
    // reported the same zero; one row with a figure was enough to pass while
    // the rest showed 0.0 MB. So each LOADED BUNDLED row owes its own figure
    // now.
    //
    // BUNDLED, which is narrower than "not the host's", and both exclusions
    // are about a container that cannot account for the module. A host-mounted
    // row is instantiated by the host rather than run by the core (ADR 0006).
    // A `web` row's module runs in the Web container, whose page is the
    // platform's process and not something the Native container measures -- so
    // it renders the em dash it is supposed to, and demanding a figure of it
    // fails a row that is behaving perfectly (seen on the iPad Air 13-inch
    // sim, #149). What is left is exactly the set #86 was about: the Bare
    // frameworks in the manifest, all of them in this process.
    const QStringList bundled = backend->bundledSetNames();
    QStringList stats;
    QStringList missingFigure;
    int loadedBundledRows = 0;
    for (const QString& name : rows) {
        QObject* row = modelRow(name);
        const bool loaded = row && row->property("isLoaded").toBool();
        const bool owesAFigure = loaded && !isHostRow(row) && bundled.contains(name);
        if (owesAFigure)
            ++loadedBundledRows;

        QQuickItem* cpu = find(QStringLiteral("moduleInspector.cpu.%1").arg(name));
        QQuickItem* memory = find(QStringLiteral("moduleInspector.memory.%1").arg(name));
        QQuickItem* folded = find(QStringLiteral("moduleInspector.stats.%1").arg(name));
        if (cpu && memory) {
            const QString cpuText = cpu->property("text").toString();
            const QString memoryText = memory->property("text").toString();
            stats << QStringLiteral("%1 %2/%3").arg(name, cpuText, memoryText);
            if (owesAFigure && !memoryText.endsWith(QLatin1String(" MB")))
                missingFigure << name;
            continue;
        }
        if (!folded) {
            emit log(QStringLiteral("WRONG: %1 has no stats cells in the table").arg(name));
            return;
        }
        // The folded line is hidden on a row with no measurement to show -- an
        // unloaded one, or a loaded one nothing could account for -- so only a
        // row that owes a figure owes it here.
        //
        // WHAT IS ON SCREEN, not what the binding computed. The folded line's
        // text is evaluated whether or not the line is shown, so reporting it
        // unconditionally printed "0.0%  ·  0.0 MB" for rows that were
        // rendering nothing at all -- which is the exact string this issue is
        // about, from a row that was innocent of it.
        const QString foldedText = folded->isVisible()
            ? folded->property("text").toString()
            : QString();
        stats << QStringLiteral("%1 %2").arg(
            name, foldedText.isEmpty() ? QStringLiteral("(no figure shown)") : foldedText);
        if (owesAFigure && !folded->isVisible()) {
            emit log(QStringLiteral("WRONG: %1 is loaded and its stats line is not on screen")
                         .arg(name));
            return;
        }
        if (owesAFigure && !foldedText.contains(QLatin1String(" MB")))
            missingFigure << name;
    }
    emit log(QStringLiteral("modules tab stats: %1").arg(stats.join(QStringLiteral(", "))));
    if (!missingFigure.isEmpty()) {
        emit log(QStringLiteral("WRONG: loaded row(s) with no memory figure: %1")
                     .arg(missingFigure.join(QStringLiteral(", "))));
        return;
    }
    emit log(loadedBundledRows > 0
                 ? QStringLiteral("SHELL MODULES TAB SHOWS THE SET'S STATS (%1 loaded "
                                  "Bundled row(s), each with a figure)").arg(loadedBundledRows)
                 : QStringLiteral("SHELL MODULES TAB SHOWS THE SET'S STATS "
                                  "(no Bundled core module is loaded, so every row it "
                                  "could measure is an em dash)"));

    // ── 3. every row's own Load/Unload button, twice ──
    //
    // EVERY row the CORE is in charge of, not just the first one with a usable
    // toggle — and only those. A row the HOST mounts is instantiated in this
    // process rather than run by the core (ADR 0006), so it says nothing about
    // the Native container; it is skipped here for the same reason it is exempt
    // from the stats check above.
    //
    // WHICH NOW INCLUDES THE `web` APPS, and that is #149. They were skipped for
    // as long as this asked for the row's TYPE: a `web` app's row is a `ui_qml`
    // row, so the check that meant "the host mounts this one" caught every app
    // whose UI is a page in the Web container -- which the core loads and
    // unloads like any other module. The Unload nobody could get to work was
    // also the Unload nothing had ever pressed.
    //
    // One row used to be enough, because the property under test was the
    // SHELL's wiring — the button, the MouseArea, the backend call — and one
    // press proves that as well as three do. #96 is what made it a property of
    // the MODULES: unloading a Bare module whose language core owns threads is
    // a different operation from unloading one that does not, and the two that
    // do here (chat_module's Rust runtime, delivery_module's nim scheduler) are
    // never the first row. A run that stopped at the first usable toggle said
    // nothing at all about the case that killed the Android process.
    QStringList driven;
    QStringList wrong;
    for (const QString& name : rows) {
        QObject* row = modelRow(name);
        if (!row || isHostRow(row))
            continue;

        QQuickItem* toggle =
            waitFor(QStringLiteral("moduleRow.loadToggle.%1").arg(name), 3000);
        if (!toggle || !toggle->isEnabled() || !toggle->isVisible())
            continue;

        // Asked once: it is the row's kind, and the three things below that
        // turn on it happen inside a single press-wait-press.
        const bool webApp = backend->isWebContainerApp(name);
        const bool before = isLoaded(name);
        // NOT DOCKED FIRST, and that is a deliberate omission with a reason.
        // The state #149 was reported in is a `web` app the user has OPENED,
        // unloaded from the Modules tab while the Shell still holds the tab its
        // page sits in -- and driving exactly that (m_host->loadUiModule(name)
        // here, then this press) works and then kills the app: the unload's
        // dropWebSurface destroys a QDockWidget, and iOS Qt faults tearing its
        // accessibility cache down. That is #139, which ShellWebAppDriver's
        // close already meets, and it would end every acceptance run one row
        // short. Measured on the iPad Air 13-inch (M2) sim, 2026-09-15: with
        // the dock, `web app web_counter_b has no page any more; its tab is
        // closed` is the last line the process prints; without it, the same
        // unload round-trips.
        //
        // The docked case IS driven, once: ShellWebAppDriver::
        // checkUnloadedWhileOpen does it last in its own pass, on the app it
        // already has open, where a death costs nothing that has not already
        // been asserted (#151). This loop still does not risk it per row.
        if (!tap(toggle)) return;
        // A `web` module's unload tears a page down and its load brings 290 MB
        // of QML runtime back up, and both are announced rather than awaited by
        // the press -- so the row is given a moment to follow the core. A Bare
        // module's toggle has already settled and pays nothing for this.
        if (webApp) settle(1500);
        QQuickItem* afterFirstToggle =
            waitFor(QStringLiteral("moduleRow.loadToggle.%1").arg(name), 3000);
        const bool afterFirst = isLoaded(name);
        // WHAT UNLOADING AN APP HAS TO DO TO THE REST OF THE SHELL (#149).
        // Between the two presses, with the module down: the container must
        // have let its page go, the Shell must not still be holding a tab onto
        // it, and the sidebar must still carry its tile -- the app is installed
        // either way, and pressing that tile is what brings it back (#123).
        if (before && !afterFirst && webApp)
            checkUnloadedWebApp(name);
        if (!afterFirstToggle || !tap(afterFirstToggle)) return;
        if (webApp) settle(1500);
        const bool afterSecond = isLoaded(name);

        emit log(QStringLiteral("drive modules: %1 %2 -> %3 -> %4")
                     .arg(name)
                     .arg(before ? QStringLiteral("loaded") : QStringLiteral("not loaded"))
                     .arg(afterFirst ? QStringLiteral("loaded") : QStringLiteral("not loaded"))
                     .arg(afterSecond ? QStringLiteral("loaded") : QStringLiteral("not loaded")));

        if (before != afterFirst && afterFirst != afterSecond && before == afterSecond)
            driven << name;
        else
            wrong << name;
    }

    if (driven.isEmpty() && wrong.isEmpty()) {
        dumpNames(QStringLiteral("no row in the Modules tab has a usable toggle"));
        return;
    }
    // NAMED, not counted. "the round trip is OK" over four rows and over one
    // look identical in a console log, and which modules were actually unloaded
    // is the whole of what #96 is about.
    emit log(wrong.isEmpty()
                 ? QStringLiteral("SHELL MODULES TAB ROUND TRIP OK (%1)")
                       .arg(driven.join(QStringLiteral(", ")))
                 : QStringLiteral("WRONG: the toggle did not round-trip for: %1")
                       .arg(wrong.join(QStringLiteral(", "))));
}
