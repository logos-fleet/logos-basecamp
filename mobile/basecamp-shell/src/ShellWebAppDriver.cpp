#include "ShellWebAppDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"
#include "WebAppSurface.h"
#include "webview/MobileWebContainerBackend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QWidget>

namespace {
// How long a `web` module gets to open its page. A phone's wasm image and the
// app's 26 MB QML runtime are seconds of work, and the page is loaded
// asynchronously on both platforms.
constexpr int kPageBudgetMs = 60000;
// How long the Shell gets to dock the placeholder and lay it out. The mount is
// queued and a QML item has no geometry until the scene has painted.
constexpr int kMountBudgetMs = 15000;
} // namespace

ShellWebAppDriver::ShellWebAppDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                     QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

QStringList ShellWebAppDriver::openWebApps() const
{
    ShellModulesBackend* backend = m_host->backend();
    QStringList apps;
    for (const QString& name :
         basecamp::web::MobileWebContainerBackend::instance()->loadedModules()) {
        if (backend->isWebContainerApp(name)) apps.append(name);
    }
    return apps;
}

QStringList ShellWebAppDriver::shippedOutsideTheBundledSet() const
{
    ShellModulesBackend* backend = m_host->backend();
    const QStringList bundled = backend->bundledSetNames();
    QStringList tree;
    for (const QString& name : backend->shippedModuleNames()) {
        if (!bundled.contains(name)) tree.append(name);
    }
    return tree;
}

void ShellWebAppDriver::loadShippedWebModules()
{
    ShellModulesBackend* backend = m_host->backend();
    for (const QString& name : shippedOutsideTheBundledSet()) {
        emit log(QStringLiteral("web app: loading the shipped `web` module %1").arg(name));
        backend->loadCoreModule(name);
    }
    QElapsedTimer since;
    since.start();
    while (openWebApps().isEmpty() && since.elapsed() < kPageBudgetMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

bool ShellWebAppDriver::hasWork() const
{
    if (!openWebApps().isEmpty()) return true;
    if (!shippedOutsideTheBundledSet().isEmpty()) return true;
    return !m_host->backend()->downloadedModules().isEmpty();
}

void ShellWebAppDriver::run()
{
    auto* web = basecamp::web::MobileWebContainerBackend::instance();

    if (openWebApps().isEmpty()) loadShippedWebModules();
    const QStringList apps = openWebApps();
    if (apps.isEmpty()) {
        emit log(QStringLiteral("web app: no `web` module in this build has a UI page"));
        return;
    }
    const QString app = apps.first();

    // ── 1. the tile, where a user would find it ──
    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    settle(200);
    QQuickItem* tile = waitFor(QStringLiteral("sidebar.app.%1").arg(app), 10000);
    if (!tile) {
        dumpNames(QStringLiteral("no sidebar tile for the web app '%1'").arg(app));
        return;
    }
    scrollIntoView(tile);
    if (!tap(tile)) return;

    // ── 2. the page is INSIDE the Shell, not over it ──
    QElapsedTimer sincePress;
    sincePress.start();
    WebAppSurface* surface = nullptr;
    while (sincePress.elapsed() < kMountBudgetMs) {
        surface = m_host->webSurface(app);
        if (surface && surface->onScreen()) break;   // i.e. it has a non-empty rect
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (!surface || !surface->onScreen()) {
        emit log(QStringLiteral("WRONG: pressing %1's tile left the Shell with no docked "
                                "surface for it").arg(app));
        return;
    }
    // The Shell's own widget tree, which only onPluginWindowRequested could
    // have put it in -- the same claim ShellAppDriver makes about a `ui_qml`
    // app's widget.
    bool inShell = false;
    for (QWidget* w = surface->parentWidget(); w; w = w->parentWidget()) {
        if (w == m_shell) { inShell = true; break; }
    }
    if (!inShell) {
        emit log(QStringLiteral("WRONG: %1's surface is not in the Shell's widget tree")
                     .arg(app));
        return;
    }

    // THE ASSERTION #110 IS ABOUT. A page that owns the window leaves nothing to
    // press; a page inset to the workspace leaves the Shell's chrome outside its
    // rect, and this is the arithmetic that says which one happened.
    const QRect page = surface->pageRect();
    const QRect windowRect = m_shell->window()->rect();
    if (page == windowRect || page.contains(windowRect)) {
        emit log(QStringLiteral("WRONG: %1's page covers the whole window (%2x%3) -- the "
                                "Shell's own navigation is underneath it and there is no "
                                "way out of the app")
                     .arg(app).arg(windowRect.width()).arg(windowRect.height()));
        return;
    }
    if (web->contentRect() != page) {
        emit log(QStringLiteral("WRONG: the Shell left %1 a %2x%3 hole and told the "
                                "container %4x%5")
                     .arg(app).arg(page.width()).arg(page.height())
                     .arg(web->contentRect().width()).arg(web->contentRect().height()));
        return;
    }
    if (web->frontmostModule() != app) {
        emit log(QStringLiteral("WRONG: %1 is docked and '%2' is the page in front")
                     .arg(app, web->frontmostModule()));
        return;
    }
    emit log(QStringLiteral("web app: %1's page is at %2,%3 %4x%5 inside a %6x%7 window -- "
                            "%8 row(s) of Shell above it and %9 below")
                 .arg(app).arg(page.x()).arg(page.y()).arg(page.width()).arg(page.height())
                 .arg(windowRect.width()).arg(windowRect.height())
                 .arg(page.top()).arg(windowRect.height() - page.bottom() - 1));

    // HELD, so that a screen recording of the run shows it. Every assertion
    // above is arithmetic on a rect, and a rect is exactly the thing a reviewer
    // cannot check by looking -- the app open INSIDE the Shell's chrome, for
    // long enough to photograph, is what makes the run worth watching.
    settle(2500);

    // ── 3. leaving it, which is the whole issue ──
    // The Settings section, which is what the sidebar's own button reaches. The
    // workspace is hidden, the placeholder goes with it, and the host puts every
    // page behind the Shell.
    m_host->setCurrentSectionIndex(ShellSection::Settings);
    settle(600);
    if (!web->frontmostModule().isEmpty() || surface->onScreen()) {
        emit log(QStringLiteral("WRONG: the user navigated away from %1 and its page is "
                                "still in front ('%2')").arg(app, web->frontmostModule()));
        return;
    }
    emit log(QStringLiteral("web app: leaving %1 put the Shell back in front of its page")
                 .arg(app));

    // ── 4. ...and coming back brings it back ──
    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    settle(600);
    if (web->frontmostModule() != app) {
        emit log(QStringLiteral("WRONG: coming back to the workspace left '%1' in front "
                                "rather than %2").arg(web->frontmostModule(), app));
        return;
    }

    // ── 5. and closing it leaves the module RUNNING ──
    // Closing an app is not an unload: the module keeps its channel and keeps
    // answering, its page simply stops covering anything.
    m_host->unloadUiModule(app);
    settle(600);
    if (m_host->webSurface(app)) {
        emit log(QStringLiteral("WRONG: %1 was closed and the Shell still holds its dock")
                     .arg(app));
        return;
    }
    if (!web->frontmostModule().isEmpty()) {
        emit log(QStringLiteral("WRONG: %1 was closed and '%2' is still in front")
                     .arg(app, web->frontmostModule()));
        return;
    }
    if (!web->hasView(app)) {
        emit log(QStringLiteral("WRONG: closing %1 destroyed its page -- closing an app is "
                                "not an unload").arg(app));
        return;
    }
    emit log(QStringLiteral("web app: %1 is closed, off screen, and still loaded")
                 .arg(app));
    emit log(QStringLiteral("SHELL OPENS A WEB APP AND THE USER CAN LEAVE IT AGAIN"));
}
