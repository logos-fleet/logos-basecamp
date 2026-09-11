#include "ShellModulesDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWidget>
#include <QWidget>

ShellModulesDriver::ShellModulesDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                       QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_shell(shellWidget)
{
}

QQuickItem* ShellModulesDriver::find(const QString& objectName) const
{
    if (!m_shell) return nullptr;
    const QList<QQuickWidget*> surfaces = m_shell->findChildren<QQuickWidget*>();
    for (QQuickWidget* surface : surfaces) {
        QQuickItem* root = surface->rootObject();
        if (!root) continue;
        if (root->objectName() == objectName) return root;
        if (QQuickItem* found = root->findChild<QQuickItem*>(objectName))
            return found;
    }
    return nullptr;
}

QQuickWidget* ShellModulesDriver::surfaceOf(QQuickItem* item) const
{
    // Walk up to the scene root, then match it against each QQuickWidget's
    // rootObject: QQuickItem has no back-pointer to the widget hosting it,
    // and the shell has more than one scene.
    if (!item || !m_shell) return nullptr;
    QQuickItem* root = item;
    while (root->parentItem()) root = root->parentItem();
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        if (surface->rootObject() == root || surface->rootObject() == item)
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
        emit log(QStringLiteral("drive: no Module Inspector section in the Settings view"));
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
        for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
            QQuickItem* root = surface->rootObject();
            if (!root) continue;
            for (QQuickItem* item : root->findChildren<QQuickItem*>()) {
                if (item->objectName().startsWith(prefix))
                    rows << item->objectName().mid(prefix.size());
            }
        }
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
        emit log(QStringLiteral("drive: no row in the Modules tab has a usable toggle"));
        return;
    }

    const auto isLoaded = [this, &driven]() -> bool {
        QQuickItem* badge = find(QStringLiteral("moduleInspector.status.%1").arg(driven));
        // The row's own badge, not the backend: what the user sees is the
        // claim, and a model that moved without the view following is the
        // failure this is looking for.
        return badge && badge->property("row").value<QObject*>()
                   ? badge->property("row").value<QObject*>()->property("isLoaded").toBool()
                   : false;
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
