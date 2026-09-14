#include "ShellModulesDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"

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

void ShellModulesDriver::run()
{
    ShellModulesBackend* backend = m_host->backend();

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
        QQuickItem* badge = find(QStringLiteral("moduleInspector.status.%1").arg(name));
        QObject* row = badge ? badge->property("row").value<QObject*>() : nullptr;
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
    // the rest showed 0.0 MB. So each LOADED CORE row owes its own figure now.
    // A view module is exempt: it is mounted by the host rather than run by the
    // core (ADR 0006), and the core has no stats for something it never loaded.
    QStringList stats;
    QStringList missingFigure;
    int loadedCoreRows = 0;
    for (const QString& name : rows) {
        QQuickItem* badge = find(QStringLiteral("moduleInspector.status.%1").arg(name));
        QObject* row = badge ? badge->property("row").value<QObject*>() : nullptr;
        const bool loaded = row && row->property("isLoaded").toBool();
        const bool isView = row
            && row->property("type").toString() == QLatin1String("ui_qml");
        const bool owesAFigure = loaded && !isView;
        if (owesAFigure)
            ++loadedCoreRows;

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
    emit log(loadedCoreRows > 0
                 ? QStringLiteral("SHELL MODULES TAB SHOWS THE SET'S STATS (%1 loaded row(s), "
                                  "each with a figure)").arg(loadedCoreRows)
                 : QStringLiteral("SHELL MODULES TAB SHOWS THE SET'S STATS "
                                  "(no core module is loaded, so every row is an em dash)"));

    // ── 3. one row's own Load/Unload button, twice ──
    // The first row that the core is actually in charge of: a view module's
    // toggle is a no-op by design (ADR 0006) and driving it would prove
    // nothing about the Native container.
    QQuickItem* toggle = nullptr;
    QString driven;
    for (const QString& name : rows) {
        QQuickItem* candidate =
            waitFor(QStringLiteral("moduleRow.loadToggle.%1").arg(name), 3000);
        if (candidate && candidate->isEnabled() && candidate->isVisible()) {
            toggle = candidate;
            driven = name;
            break;
        }
    }
    if (!toggle) {
        dumpNames(QStringLiteral("no row in the Modules tab has a usable toggle"));
        return;
    }

    // The row's own badge, not the backend: what the user sees is the claim,
    // and a model that moved without the view following is the failure this is
    // looking for.
    const auto isLoaded = [this, &driven]() -> bool {
        QQuickItem* badge = find(QStringLiteral("moduleInspector.status.%1").arg(driven));
        QObject* row = badge ? badge->property("row").value<QObject*>() : nullptr;
        return row && row->property("isLoaded").toBool();
    };

    const bool before = isLoaded();
    if (!tap(toggle)) return;
    QQuickItem* afterFirstToggle =
        waitFor(QStringLiteral("moduleRow.loadToggle.%1").arg(driven), 3000);
    const bool afterFirst = isLoaded();
    if (!afterFirstToggle || !tap(afterFirstToggle)) return;
    const bool afterSecond = isLoaded();

    emit log(QStringLiteral("drive modules: %1 %2 -> %3 -> %4")
                 .arg(driven)
                 .arg(before ? QStringLiteral("loaded") : QStringLiteral("not loaded"))
                 .arg(afterFirst ? QStringLiteral("loaded") : QStringLiteral("not loaded"))
                 .arg(afterSecond ? QStringLiteral("loaded") : QStringLiteral("not loaded")));
    emit log(before != afterFirst && afterFirst != afterSecond && before == afterSecond
                 ? QStringLiteral("SHELL MODULES TAB ROUND TRIP OK")
                 : QStringLiteral("WRONG: the row's toggle did not round-trip"));
}
