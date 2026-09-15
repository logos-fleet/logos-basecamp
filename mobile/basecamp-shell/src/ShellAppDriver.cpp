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
// How long a mount gets to put the app's handles on screen: the launch is
// queued, the framework dlopens the plugin and the replica waits for its
// source -- seconds of work on a phone. Spent once per mount, and step 6
// mounts the app a second time.
constexpr int kMountBudgetMs = 30000;
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

ShellAppDriver::KnownApp ShellAppDriver::knownApp(const QString& appName)
{
    // chat_ui's conversations pane and its "+" menu: the two things on screen
    // before any conversation is selected, and both are in the app's own QML
    // (logos-chat-ui src/qml/ChatUi/ConversationsPane.qml) rather than in
    // anything this repo draws. The pane is also its content list -- the rows
    // in it are the core's conversations.
    if (appName == QLatin1String("chat_ui")) {
        const QString pane = QStringLiteral("conversationList");
        // 10 s is ChatBackend's own kHealthIntervalMs. Named here rather than
        // guessed: the re-open check below has to outlast one tick of it.
        return { { pane, QStringLiteral("newMenuButton") }, pane, 10000 };
    }
    // view_counter's button and label -- the fixture's whole view. It lists
    // nothing, so there is no content to check it against.
    if (appName == QLatin1String("view_counter"))
        return { { QStringLiteral("incrementButton"), QStringLiteral("countLabel") }, { } };
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
    const KnownApp known = knownApp(app);
    const QStringList& handles = known.handles;
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

    // The sidebar's app column is a vertical Flickable, and a set with several
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
        QQuickItem* item = waitFor(handle, kMountBudgetMs);
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
    // Read into locals rather than off `view` at the summary below: step 6
    // closes this mount, and the widget the host handed the Shell is deleted
    // with it -- so by the time that line runs there is nothing left to ask.
    const int shownWidth = view->width();
    const int shownHeight = view->height();
    if (shownWidth <= 0 || shownHeight <= 0) {
        emit log(QStringLiteral("WRONG: '%1' is mounted in the Shell at %2x%3")
                     .arg(app).arg(shownWidth).arg(shownHeight));
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
    const QString& listHandle = known.contentList;
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

    // ── 6. and the user can close it and open it again ──
    //
    // A docked app has a close button, and pressing it destroys the whole mount
    // -- the view module's plugin object, its replica node, and the LogosAPI the
    // host built for THAT mount. Opening the app again builds a second set. No
    // run ever did that pair, and the pair is exactly what broke: the
    // module-to-module transport cached the first mount's identity object for
    // the life of the PROCESS, so the second mount's first call out ran on freed
    // memory -- a SIGSEGV inside TokenManager::getToken, reached from chat_ui's
    // health probe (logos-workspace#158).
    //
    // Through IShellHost rather than by pressing the tab's close button: the
    // button is the Shell's own chrome and ShellWebAppDriver already closes a
    // web app through this same call. What is under test here is the MOUNT, not
    // the control that asks for it.
    m_host->unloadUiModule(app);
    // Long enough for the deferred deletes the close queues -- the widget first,
    // the runner behind it -- to actually run. Re-opening before they do would
    // leave the first mount's LogosAPI alive and prove nothing.
    settle(1500);
    if (m_host->mountedView(app)) {
        emit log(QStringLiteral("WRONG: '%1' was closed and the host still holds a view "
                                "for it").arg(app));
        return;
    }
    m_host->loadUiModule(app);
    for (const QString& handle : handles) {
        if (!waitFor(handle, kMountBudgetMs)) {
            dumpNames(QStringLiteral("app '%1' was closed and opened again and has no "
                                     "'%2' on screen").arg(app, handle));
            return;
        }
    }
    if (!m_host->mountedView(app)) {
        emit log(QStringLiteral("WRONG: '%1' rendered again and the host holds no view "
                                "for it").arg(app));
        return;
    }
    // DRAWN IS NOT ENOUGH, and the call that matters is not the mount's. The
    // frame the report faulted in is chat_ui's periodic health probe -- the
    // second mount's timer, calling out through the transport the first mount
    // left behind -- so the loop is turned for a full probe interval and a
    // margin, which is what makes that call happen inside the run rather than
    // after it. An app with no such timer asks for nothing and pays nothing.
    if (known.probeIntervalMs > 0)
        settle(known.probeIntervalMs + 2000);
    else
        settle(1000);
    if (!m_host->mountedView(app)) {
        emit log(QStringLiteral("WRONG: '%1' came back and did not stay").arg(app));
        return;
    }
    emit log(QStringLiteral("shell app: %1 was closed and opened again, and the second "
                            "mount is live").arg(app));

    emit log(QStringLiteral("shell app: %1 rendered %2 in the Shell (%3x%4) %5 ms after "
                            "the tile was pressed")
                 .arg(app, handles.join(QStringLiteral(", ")))
                 .arg(shownWidth).arg(shownHeight).arg(shownMs));
    emit log(QStringLiteral("SHELL SHOWS THE BUNDLED APP"));
    emit appShown(app, shownMs);
}
