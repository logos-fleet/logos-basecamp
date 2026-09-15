#include "BundledSetShellHost.h"

#include "ShellSections.h"
#include "ViewModuleRunner.h"
#include "WebAppSurface.h"
#include "webview/MobileWebContainerBackend.h"

#include <QQuickWidget>
#include <QTimer>

#include <utility>

BundledSetShellHost::BundledSetShellHost(BundledSetCoreRuntime* core)
    : m_backend(core)
{
    // The sidebar writes the section index straight into the backend; the
    // content stack only follows if the observer hears about it.
    QObject::connect(&m_backend, &ShellModulesBackend::currentActiveSectionIndexChanged,
                     &m_backend, [this] {
                         if (m_observer)
                             m_observer->onSectionIndexChanged(m_backend.currentActiveSectionIndex());
                     });

    // The sidebar's tiles and IShellHost::loadUiModule both arrive here. The
    // backend raises the request because it is what the Shell's QML can see;
    // the mount happens here because it is what owns the observer.
    QObject::connect(&m_backend, &ShellModulesBackend::uiModuleLaunchRequested,
                     &m_backend, [this](const QString& name) { mountApp(name); });
    QObject::connect(&m_backend, &ShellModulesBackend::uiModuleCloseRequested,
                     &m_backend, [this](const QString& name) { unmountApp(name); });

    // A DOCK MEANS A PAGE, and the container is the only thing that knows when
    // one stops existing. A `web` app's page goes away for reasons that are not
    // the user closing it -- the live-runtime budget unloads a module that
    // ships no headless document, the module is unloaded from the Modules tab
    // -- and the placeholder used to outlive it: an empty tab the Shell would
    // raise whenever the one in front was closed, claiming a module was in
    // front of a page that no longer existed.
    QObject::connect(basecamp::web::MobileWebContainerBackend::instance(),
                     &basecamp::web::MobileWebContainerBackend::viewClosed,
                     &m_backend, [this](const QString& name) { dropWebSurface(name); });
}

BundledSetShellHost::~BundledSetShellHost()
{
    // The runners, not the widgets: a mounted widget was handed to the Shell
    // and the Shell's own teardown deletes it. Each runner drops its replica,
    // its node and its plugin object -- never the image, which still holds the
    // code all three were running (see ~ViewModuleRunner).
    for (const Mounted& mounted : m_mounted)
        delete mounted.runner;
    m_mounted.clear();
    // The placeholders are the Shell's for the same reason the widgets are: each
    // was handed over through onPluginWindowRequested. Forgetting them is all
    // this side has to do.
    m_webSurfaces.clear();
}

QObject* BundledSetShellHost::backendObject() { return &m_backend; }

int BundledSetShellHost::currentSectionIndex() const
{
    return m_backend.currentActiveSectionIndex();
}

void BundledSetShellHost::setCurrentSectionIndex(int index)
{
    m_backend.setCurrentActiveSectionIndex(index);
}

void BundledSetShellHost::loadUiModule(const QString& name)   { mountApp(name); }
void BundledSetShellHost::unloadUiModule(const QString& name) { unmountApp(name); }

QQuickWidget* BundledSetShellHost::mountedView(const QString& name) const
{
    return m_mounted.value(name).widget;
}

WebAppSurface* BundledSetShellHost::webSurface(const QString& name) const
{
    return m_webSurfaces.value(name);
}

void BundledSetShellHost::mountApp(const QString& name)
{
    const auto already = m_mounted.constFind(name);
    if (already != m_mounted.cend()) {
        m_backend.setCurrentVisibleApp(name);
        if (m_observer)
            m_observer->onPresentAppRequested(already->widget);
        return;
    }
    // A WEB-CONTAINER APP IS NOT A WIDGET, and that is the whole difference. A
    // Bundled `ui_qml` member is a framework this process instantiates, whose
    // QML goes into a QQuickWidget the Shell docks. A module the core
    // discovered has its UI in a PAGE the Web container already opened when the
    // core loaded it, mounted by the platform at the window's size and sitting
    // behind the Shell's own surface -- so "open it" is a z-order instruction
    // and there is no widget to hand the observer
    // (webview/MobileWebContainerBackend.h).
    //
    // WHICH IS WHY IT GETS A PLACEHOLDER. See mountWebApp.
    if (m_backend.isWebContainerApp(name)) {
        mountWebApp(name);
        return;
    }
    if (!m_backend.isViewModule(name)) {
        // AND TELL THE SHELL. A Store shell's Bundled set is data (ADR 0007),
        // so this is the ordinary answer for a desktop `ui_qml` plugin on a
        // phone -- package_manager_ui above all, which the Shell mounts into a
        // page of its own and used to wait for forever (logos-workspace#145).
        // The report alone reaches the Modules tab's log and nothing the user
        // looking at that page can see.
        const QString why = QStringLiteral("app %1 is not a view module in this Bundled set")
                                .arg(name);
        m_backend.report(why);
        if (m_observer) m_observer->onUiModuleUnavailable(name, why);
        return;
    }

    // SizeRootObjectToView, because the Shell decides how big an app is: the
    // dock it goes into is laid out by the workspace, and a view sized by its
    // own implicitWidth would render a 1000x700 desktop window inside it.
    auto* surface = new QQuickWidget;
    surface->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The stem is the module's name plus the suffix logos-module-builder gives
    // a `view` output. Derived rather than compiled in: a Bundled set may
    // carry several apps and which one is mounted is the user's choice.
    auto* runner = new ViewModuleRunner(name + QStringLiteral("_view"), &m_backend);
    QObject::connect(runner, &ViewModuleRunner::log,
                     &m_backend, &ShellModulesBackend::report);
    if (!runner->run(surface)) {
        const QString why = QStringLiteral("app %1 did not come up").arg(name);
        m_backend.report(why);
        delete runner;
        delete surface;
        if (m_observer) m_observer->onUiModuleUnavailable(name, why);
        return;
    }

    m_mounted.insert(name, Mounted{ surface, runner });
    m_backend.setUiModuleMounted(name, true);
    m_backend.setCurrentVisibleApp(name);
    if (m_observer)
        m_observer->onPluginWindowRequested(surface, name);
    m_backend.report(QStringLiteral("app %1 is mounted in the Shell").arg(name));
}

void BundledSetShellHost::unmountApp(const QString& name)
{
    // Closing a web-container app is the z-order instruction in reverse, and it
    // is NOT an unload: the module stays loaded and keeps answering calls, its
    // page simply stops covering the Shell. Nothing is destroyed behind the
    // container's back.
    if (m_backend.isWebContainerApp(name)) {
        // The placeholder goes out of the Shell exactly as a `ui_qml` widget
        // does -- ask first, then delete -- and every page goes behind the Shell
        // right here rather than on the next sync: the report below already says
        // the app is off screen, and a caller that read the container in between
        // would have been told otherwise.
        if (WebAppSurface* surface = m_webSurfaces.take(name)) {
            if (m_observer)
                m_observer->onPluginWindowRemoveRequested(surface);
            surface->deleteLater();
        }
        auto* web = basecamp::web::MobileWebContainerBackend::instance();
        web->hideAll();
        m_webVisible.clear();
        web->setContentRect(QRect());
        if (m_backend.currentVisibleApp() == name)
            m_backend.setCurrentVisibleApp(QString());
        m_backend.report(QStringLiteral("web app %1 is off screen and still "
                                        "running").arg(name));
        return;
    }
    const auto it = m_mounted.constFind(name);
    if (it == m_mounted.cend()) {
        m_backend.report(QStringLiteral("app %1 is not mounted").arg(name));
        return;
    }
    const Mounted mounted = it.value();
    m_mounted.erase(it);
    // Ask first, then delete: the Shell has the widget in a dock, and taking
    // it out is what makes deleting it safe. The desktop host has the same
    // order for the same reason (IShellObserver's contract).
    if (m_observer)
        m_observer->onPluginWindowRemoveRequested(mounted.widget);
    // BOTH deferred, and in this order: the widget still holds the module's
    // QML scene, whose bindings reach the runner's bridge. Deleting the runner
    // here would leave those bindings pointing at freed objects for exactly as
    // long as the widget outlives it -- which is until the next loop turn.
    // Queued, the widget goes first and the scene is gone before the bridge is.
    mounted.widget->deleteLater();
    mounted.runner->deleteLater();
    m_backend.setUiModuleMounted(name, false);
    if (m_backend.currentVisibleApp() == name)
        m_backend.setCurrentVisibleApp(QString());
    m_backend.report(QStringLiteral("app %1 is unmounted").arg(name));
}

// ── #110: A WEB APP NAVIGATES LIKE EVERY OTHER APP ──────────────────────────
//
// The page is the platform's and is mounted at the WINDOW's size, so bringing it
// forward used to cover the Shell whole: no sidebar, no tab bar, no close
// button, and no way back out of the app once it was open. Nothing was wrong
// with the page -- there was simply nothing left on screen to press.
//
// So a web app is DOCKED, like a `ui_qml` one. What goes into the dock is a
// placeholder with no pixels in it (WebAppSurface), and the Shell draws the same
// chrome around it that it draws around any app: a tab with a close button, the
// sidebar beside it, the navigation above. The placeholder then says where the
// workspace put it and the page is inset to exactly that rect -- so the chrome
// is beside the page rather than under it, and closing the tab reaches
// unloadUiModule() through the path every other app already uses.
void BundledSetShellHost::mountWebApp(const QString& name)
{
    auto* web = basecamp::web::MobileWebContainerBackend::instance();

    // THE MODULE MAY NOT BE RUNNING YET, and on the second launch of an app the
    // user installed it never is: the core discovers the package and waits to
    // be asked, because a Store shell's cold start deliberately does not ask
    // (#123). The sidebar carries the tile anyway -- the package's manifest is
    // what says it has a UI, not its page -- so the PRESS is what brings the
    // module up, and the page it opens is what there is to show.
    //
    // Deliberately here rather than at startup: one page is 290 MB of QML
    // runtime and seconds of it, and the container's budget is one live
    // runtime, so an app loaded because the user asked for it is the only one
    // that can be loaded without taking another's page away.
    //
    // BEFORE the docked-already branch, not after it: a module can lose its
    // page while the Shell still holds its tab, and raising that tab without
    // this would present an empty one.
    if (!web->hasView(name)) {
        if (!m_backend.ensureRunning(name)) {
            m_backend.report(QStringLiteral("web app %1 is not on this device").arg(name));
            return;
        }
        if (!web->hasView(name)) {
            m_backend.report(QStringLiteral("web app %1 came up with no page; there is "
                                            "nothing to put on screen").arg(name));
            return;
        }
    }

    if (WebAppSurface* already = m_webSurfaces.value(name)) {
        m_backend.setCurrentVisibleApp(name);
        if (m_observer)
            m_observer->onPresentAppRequested(already);
        queueWebSync();
        return;
    }

    auto* surface = new WebAppSurface(name);
    QObject::connect(surface, &WebAppSurface::placementChanged,
                     &m_backend, [this] { queueWebSync(); });
    m_webSurfaces.insert(name, surface);
    m_backend.setCurrentVisibleApp(name);

    // The page comes forward NOW rather than on the sync, so the module the user
    // pressed is up in the same frame it always was; the sync that follows only
    // decides where it sits. A Shell that never docks the placeholder -- a host
    // with no observer, which is every driver that builds this class alone --
    // then behaves exactly as it did before.
    web->show(name);
    m_webVisible = name;
    if (m_observer)
        m_observer->onPluginWindowRequested(surface, name);
    queueWebSync();
    m_backend.report(QStringLiteral("web app %1 is on screen").arg(name));
}

void BundledSetShellHost::dropWebSurface(const QString& name)
{
    WebAppSurface* surface = m_webSurfaces.take(name);
    if (!surface) return;
    if (m_observer)
        m_observer->onPluginWindowRemoveRequested(surface);
    surface->deleteLater();
    if (m_webVisible == name) m_webVisible.clear();
    if (m_backend.currentVisibleApp() == name)
        m_backend.setCurrentVisibleApp(QString());
    m_backend.report(QStringLiteral("web app %1 has no page any more; its tab is closed")
                         .arg(name));
    // Whatever is left decides what is in front now, by the one rule that
    // decides it everywhere else.
    queueWebSync();
}

void BundledSetShellHost::queueWebSync()
{
    if (m_webSyncQueued) return;
    m_webSyncQueued = true;
    QTimer::singleShot(0, &m_backend, [this] { syncWebSurfaces(); });
}

void BundledSetShellHost::syncWebSurfaces()
{
    m_webSyncQueued = false;
    auto* web = basecamp::web::MobileWebContainerBackend::instance();

    // THE ONE THAT IS ACTUALLY ON SCREEN. At most one can be: a dock raises one
    // tab, and the budget keeps one runtime alive anyway. A placeholder the
    // Shell has hidden -- another tab raised, the user gone to Settings -- is
    // not it, and that is what makes "leave the app" a thing the Shell can say.
    WebAppSurface* front = nullptr;
    for (WebAppSurface* surface : std::as_const(m_webSurfaces)) {
        if (surface->onScreen()) { front = surface; break; }
    }

    if (!front) {
        // Z-ORDER FIRST, then the rect. The other order leaves a page briefly
        // occupying the whole window in front of the Shell, which is the defect
        // itself for the length of a frame.
        if (!m_webVisible.isEmpty()) {
            web->hideAll();
            m_webVisible.clear();
            m_backend.report(QStringLiteral("no web app is on screen; the Shell has the "
                                            "window back"));
        }
        web->setContentRect(QRect());
        return;
    }

    web->setContentRect(front->pageRect());
    if (m_webVisible != front->moduleName()) {
        web->show(front->moduleName());
        m_webVisible = front->moduleName();
        m_backend.setCurrentVisibleApp(front->moduleName());
    }
}

void BundledSetShellHost::setCurrentVisibleApp(const QString& name)
{
    m_backend.setCurrentVisibleApp(name);
}

QString BundledSetShellHost::displayNameFor(const QString& name) const { return name; }

void BundledSetShellHost::setObserver(IShellObserver* observer) { m_observer = observer; }

void BundledSetShellHost::replaySection()
{
    if (m_observer && m_backend.currentActiveSectionIndex() != ShellSection::Workspace)
        m_observer->onSectionIndexChanged(m_backend.currentActiveSectionIndex());
}
