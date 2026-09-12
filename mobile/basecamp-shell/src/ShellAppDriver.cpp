#include "ShellAppDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVariant>

namespace {
// How long a remoted model gets to deliver its first row. Generous: the
// replica's fetch crosses the node, and the backend's own snapshot call is a
// module hop behind it.
constexpr int kModelBudgetMs = 15000;
} // namespace

ShellAppDriver::ShellAppDriver(BundledSetShellHost* host, QWidget* shellWidget,
                               QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

bool ShellAppDriver::hasWork() const
{
    return !m_host->backend()->viewModuleNames().isEmpty();
}

QStringList ShellAppDriver::handlesFor(const QString& appName)
{
    // chat_ui's conversations pane and its "+" menu: the two things on screen
    // before any conversation is selected, and both are in the app's own QML
    // (logos-chat-ui src/qml/ChatUi/ConversationsPane.qml) rather than in
    // anything this repo draws.
    if (appName == QLatin1String("chat_ui"))
        return { QStringLiteral("conversationList"), QStringLiteral("newMenuButton") };
    // view_counter's button and label -- the fixture's whole view.
    if (appName == QLatin1String("view_counter"))
        return { QStringLiteral("incrementButton"), QStringLiteral("countLabel") };
    return { };
}

QString ShellAppDriver::contentListFor(const QString& appName)
{
    if (appName == QLatin1String("chat_ui"))
        return QStringLiteral("conversationList");
    return { };
}

void ShellAppDriver::run(bool expectLiveContent)
{
    ShellModulesBackend* backend = m_host->backend();
    const QStringList apps = backend->viewModuleNames();
    if (apps.isEmpty()) {
        emit log(QStringLiteral("shell apps: none in this Bundled set"));
        return;
    }
    const QString app = apps.first();
    const QStringList handles = handlesFor(app);
    if (handles.isEmpty()) {
        emit log(QStringLiteral("WRONG: no rendered handles are known for app '%1', so "
                                "there is nothing that could say its view came up")
                     .arg(app));
        return;
    }

    // ── 1. the tile, where a user would find it ──
    // The Workspace section is the HOST's to set, exactly as the Modules
    // driver sets Settings; the tile inside it is the Shell's and is pressed.
    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QString tileHandle = QStringLiteral("sidebar.app.%1").arg(app);
    QQuickItem* tile = waitFor(tileHandle, 5000);
    if (!tile) {
        dumpNames(QStringLiteral("no sidebar tile for the Bundled app '%1'").arg(app));
        return;
    }
    emit log(QStringLiteral("shell: the sidebar carries a tile for %1").arg(app));

    // The sidebar's app column is a vertical Flickable; a set with several
    // apps can push one of them below the fold.
    scrollIntoView(tile);
    QElapsedTimer sincePress;
    sincePress.start();
    if (!tap(tile)) return;

    // ── 2. the app's own QML, on screen ──
    // The mount is queued (MainContainer connects launchUIModule with a
    // QueuedConnection, for the delegate-lifetime reason stated there), the
    // framework's dlopen and the replica's waitForSource are seconds of work
    // on a phone, and the QML then has to instantiate. So this waits for the
    // app's handles rather than for the call to return.
    for (const QString& handle : handles) {
        QQuickItem* item = waitFor(handle, 30000);
        if (!item) {
            dumpNames(QStringLiteral("app '%1' has no '%2' on screen").arg(app, handle));
            return;
        }
        if (!item->isVisible()) {
            emit log(QStringLiteral("WRONG: app '%1' rendered '%2' but it is not visible")
                         .arg(app, handle));
            return;
        }
    }
    const qint64 shownMs = sincePress.elapsed();

    // ── 3. and it is the SHELL that is showing it ──
    // A QQuickWidget that rendered while parented to nothing would satisfy
    // everything above. What makes this the Shell's app is that the widget the
    // host created is inside the Shell's own widget tree, which only the
    // observer's onPluginWindowRequested could have put it there.
    QQuickWidget* view = m_host->mountedView(app);
    if (!view) {
        emit log(QStringLiteral("WRONG: '%1' rendered but the host holds no view for it")
                     .arg(app));
        return;
    }
    bool inShell = false;
    for (QWidget* w = view->parentWidget(); w; w = w->parentWidget()) {
        if (w == m_shell) { inShell = true; break; }
    }
    if (!inShell) {
        emit log(QStringLiteral("WRONG: '%1' rendered outside the Shell -- the workspace "
                                "never took the widget").arg(app));
        return;
    }
    if (view->width() <= 0 || view->height() <= 0) {
        emit log(QStringLiteral("WRONG: '%1' is mounted in the Shell at %2x%3")
                     .arg(app).arg(view->width()).arg(view->height()));
        return;
    }

    // ── 4. and the row moved with it ──
    // The Modules tab's claim is "what is running". An app the host just
    // mounted has to read as loaded there, and the sidebar has to have moved
    // it into its loaded group.
    const bool listedLoaded = [&] {
        for (const QVariant& row : backend->launcherApps()) {
            const QVariantMap entry = row.toMap();
            if (entry.value(QStringLiteral("name")).toString() == app)
                return entry.value(QStringLiteral("isLoaded")).toBool();
        }
        return false;
    }();
    if (!listedLoaded) {
        emit log(QStringLiteral("WRONG: '%1' is on screen and the app list still calls it "
                                "not loaded").arg(app));
        return;
    }

    // ── 5. and it is showing what the core already has ──
    // The backend replica and the models are separate remoted sources: the
    // first is the .rep, each of the others is a child source acquired by
    // name. So a view can be fully bound, online, and listing nothing -- which
    // is exactly what an unremoted model looks like from the outside.
    const QString listHandle = contentListFor(app);
    if (!listHandle.isEmpty()) {
        QQuickItem* list = find(listHandle);
        // A model replica's row count arrives over the node, so it is NOT
        // there in the tick the view instantiated: reading `count` straight
        // after the QML loaded reports zero for a list that is about to fill.
        // Polled, and only until a row shows up -- an app that really has
        // nothing to list still costs the budget once.
        QElapsedTimer settle;
        settle.start();
        while (list && list->property("count").toInt() < 1
               && settle.elapsed() < kModelBudgetMs)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const int count = list ? list->property("count").toInt() : -1;
        emit log(QStringLiteral("shell app: %1's %2 holds %3 row(s) after %4 ms")
                     .arg(app, listHandle).arg(count).arg(settle.elapsed()));
        if (expectLiveContent && count < 1) {
            emit log(QStringLiteral("WRONG: the Shell's own run left a conversation in the "
                                    "module and %1 shows %2 -- the app is not bound to the "
                                    "core's data").arg(app).arg(count));
            return;
        }
    }

    emit log(QStringLiteral("shell app: %1 rendered %2 in the Shell (%3x%4) %5 ms after "
                            "the tile was pressed")
                 .arg(app, handles.join(QStringLiteral(", ")))
                 .arg(view->width()).arg(view->height()).arg(shownMs));
    emit log(QStringLiteral("SHELL SHOWS THE BUNDLED APP"));
    emit appShown(app, shownMs);
}
