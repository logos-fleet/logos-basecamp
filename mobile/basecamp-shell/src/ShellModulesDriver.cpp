#include "ShellModulesDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaObject>
#include <QMouseEvent>
#include <QRectF>
#include <QQuickItem>
#include <QQuickWidget>
#include <QUrl>
#include <QWidget>

#include <functional>

namespace {

void walkItems(QQuickItem* item, const std::function<void(QQuickItem*)>& visit)
{
    if (!item) return;
    visit(item);
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children)
        walkItems(child, visit);
}

} // namespace

ShellModulesDriver::ShellModulesDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                       QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_shell(shellWidget)
{
}

void ShellModulesDriver::dumpNames(const QString& why)
{
    // Every named item in every scene the shell owns. Printed only when a
    // lookup has already failed, and it is the whole diagnosis: a handle that
    // is absent, one that is spelled differently, or a scene this host cannot
    // see into are three different bugs with the same symptom.
    emit log(QStringLiteral("drive: %1 -- named items in the shell's scenes:").arg(why));
    if (!m_shell) return;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        QQuickItem* root = surface->rootObject();
        emit log(QStringLiteral("  scene '%1' (root %2)")
                     .arg(surface->source().toString(),
                          root ? root->objectName() : QStringLiteral("<none>")));
        if (!root) continue;
        QStringList named;
        walkItems(root, [&named](QQuickItem* item) {
            if (!item->objectName().isEmpty())
                named << item->objectName();
        });
        emit log(QStringLiteral("    %1")
                     .arg(named.isEmpty() ? QStringLiteral("(nothing named)")
                                          : named.join(QStringLiteral(", "))));
    }
}

void ShellModulesDriver::forEachItem(const std::function<void(QQuickItem*)>& visit) const
{
    if (!m_shell) return;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>())
        walkItems(surface->rootObject(), visit);
}

QQuickItem* ShellModulesDriver::find(const QString& objectName) const
{
    QQuickItem* found = nullptr;
    forEachItem([&found, &objectName](QQuickItem* item) {
        if (!found && item->objectName() == objectName)
            found = item;
    });
    return found;
}

QQuickWidget* ShellModulesDriver::surfaceOf(QQuickItem* item) const
{
    // By WINDOW, not by walking up to the root object: a QQuickWidget renders
    // into an offscreen QQuickWindow whose contentItem sits ABOVE rootObject,
    // so "walk to the topmost parentItem" lands one level past the root and
    // matches nothing. The shell has four scenes and every item knows which
    // window it is in.
    if (!item || !m_shell) return nullptr;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        if (surface->quickWindow() == item->window())
            return surface;
    }
    return nullptr;
}

QQuickItem* ShellModulesDriver::waitFor(const QString& objectName, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (QQuickItem* item = find(objectName))
            return item;
        if (t.elapsed() >= timeoutMs)
            return nullptr;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

bool ShellModulesDriver::tap(QQuickItem* item)
{
    QQuickWidget* surface = surfaceOf(item);
    if (!surface) {
        emit log(QStringLiteral("drive: '%1' is in no scene this host owns")
                     .arg(item ? item->objectName() : QString()));
        return false;
    }
    // Scene coordinates ARE widget coordinates for a QQuickWidget, so the
    // centre of the item in the scene is where the press goes.
    const QPointF centre = item->mapToScene(
        QPointF(item->width() / 2.0, item->height() / 2.0));

    // ...but only if the scene actually shows that point. Qt delivers a press
    // by COORDINATE, so a point outside the viewport is not "a press on a
    // scrolled-away button", it is a press on whatever is at that coordinate
    // -- which is silence, and looks exactly like a button that does nothing.
    //
    // This is not hypothetical on a phone: the Module Inspector's table is the
    // desktop's, about a thousand logical pixels of columns, and the action
    // column is the last of them. Making the Shell's tables narrow enough for
    // a handset is a slice of its own; until then, say which of the two
    // happened.
    if (!QRectF(QPointF(0, 0), QSizeF(surface->size())).contains(centre)) {
        emit log(QStringLiteral("drive: '%1' is at (%2, %3), outside the %4x%5 viewport "
                                "-- the Shell's desktop table is wider than this screen; "
                                "activating the control instead of pressing it")
                     .arg(item->objectName())
                     .arg(centre.x(), 0, 'f', 0).arg(centre.y(), 0, 'f', 0)
                     .arg(surface->width()).arg(surface->height()));
        // Still the Shell's own control and the Shell's own signal chain --
        // LogosButton.onClicked -> loadToggleRequested -> ModuleInspectorView
        // -> SettingsView -> ContentViews -> the backend. The only thing
        // skipped is UIKit's delivery of the touch to a pixel that is not on
        // this screen.
        if (!QMetaObject::invokeMethod(item, "clicked")) {
            emit log(QStringLiteral("drive: '%1' has no clicked() to activate")
                         .arg(item->objectName()));
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return true;
    }

    const QPointF global = surface->mapToGlobal(centre);
    QMouseEvent press(QEvent::MouseButtonPress, centre, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QGuiApplication::sendEvent(surface, &press);
    QGuiApplication::sendEvent(surface, &release);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return true;
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
    if (!tap(section)) return;

    QQuickItem* view = waitFor(QStringLiteral("moduleInspectorView"), 5000);
    if (!view || !view->isVisible()) {
        emit log(QStringLiteral("drive: the Module Inspector did not come to the front"));
        return;
    }
    emit log(QStringLiteral("shell: Settings -> Module Inspector is on screen"));

    // ── 2. the rows on screen ARE the Bundled set ──
    // Counted off the SCENE, not off the model: the model is built from the
    // manifest, so the two agreeing would prove only that this host can copy
    // a list. What can still go wrong above it is the view -- a filter proxy
    // dropping a row, a delegate that never instantiated, a table showing a
    // module the manifest does not account for -- and each row's status badge
    // carries its module's name, so the badges ARE the rendered list.
    const QStringList set = backend->bundledSetNames();
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
    emit log(QStringLiteral("bundled set:      %1").arg(expected.join(QStringLiteral(", "))));
    if (rows != expected) {
        emit log(QStringLiteral("WRONG: the Modules tab does not list the Bundled set"));
        return;
    }
    emit log(QStringLiteral("SHELL MODULES TAB LISTS THE BUNDLED SET"));

    // ── 2b. every row came in with the app image ──
    // A Store shell may not gain a native module at runtime (ADR 0003), so
    // "the app contains no Downloaded module" is not a thing to hope for --
    // it is a thing the tab must be able to SAY. `installType` is the column
    // Basecamp answers that with everywhere else, and "embedded" is its
    // answer for something the build put there. Anything else in this table
    // would be a module that arrived some other way.
    QStringList notEmbedded;
    for (const QString& name : rows) {
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
    emit log(QStringLiteral("all %1 rows are installType 'embedded' -- no Downloaded module")
                 .arg(rows.size()));

    // ── 2c. and the tab shows their stats ──
    // Off the rendered CELLS, for the same reason the row list is counted off
    // the scene: the model carries cpu and memory whether or not the table
    // ever drew them, and an unloaded row deliberately renders an em dash. So
    // a loaded row must show a figure, and it is the figure on screen that
    // has to be one.
    QStringList stats;
    bool sawFigure = false;
    for (const QString& name : rows) {
        QQuickItem* cpu = find(QStringLiteral("moduleInspector.cpu.%1").arg(name));
        QQuickItem* memory = find(QStringLiteral("moduleInspector.memory.%1").arg(name));
        if (!cpu || !memory) {
            emit log(QStringLiteral("WRONG: %1 has no stats cells in the table").arg(name));
            return;
        }
        const QString cpuText = cpu->property("text").toString();
        const QString memoryText = memory->property("text").toString();
        stats << QStringLiteral("%1 %2/%3").arg(name, cpuText, memoryText);
        if (memoryText.endsWith(QLatin1String(" MB")))
            sawFigure = true;
    }
    emit log(QStringLiteral("modules tab stats: %1").arg(stats.join(QStringLiteral(", "))));
    if (!sawFigure) {
        emit log(QStringLiteral("WRONG: no row in the Modules tab shows a memory figure"));
        return;
    }
    emit log(QStringLiteral("SHELL MODULES TAB SHOWS THE SET'S STATS"));

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
