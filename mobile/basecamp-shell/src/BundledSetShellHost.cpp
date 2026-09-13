#include "BundledSetShellHost.h"

#include "ShellSections.h"
#include "ViewModuleRunner.h"
#include "webview/MobileWebContainerBackend.h"

#include <QQuickWidget>

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

void BundledSetShellHost::mountApp(const QString& name)
{
    const auto already = m_mounted.constFind(name);
    if (already != m_mounted.cend()) {
        m_backend.setCurrentVisibleApp(name);
        if (m_observer)
            m_observer->onPresentAppRequested(already->widget);
        return;
    }
    // A DOWNLOADED APP IS NOT A WIDGET, and that is the whole difference. A
    // Bundled `ui_qml` member is a framework this process instantiates, whose
    // QML goes into a QQuickWidget the Shell docks. A Downloaded module's UI is
    // a PAGE the Web container already opened when the core loaded it, mounted
    // by the platform at the window's size and sitting behind the Shell's own
    // surface -- so "open it" is a z-order instruction and there is no widget to
    // hand the observer (webview/MobileWebContainerBackend.h).
    //
    // show() is also what spends the live-runtime budget: it is the sentence
    // "the user is looking at this module", and the container answers it by
    // giving some other module's page up.
    if (m_backend.isDownloadedApp(name)) {
        basecamp::web::MobileWebContainerBackend::instance()->show(name);
        m_backend.setCurrentVisibleApp(name);
        m_backend.report(QStringLiteral("downloaded app %1 is on screen").arg(name));
        return;
    }
    if (!m_backend.isViewModule(name)) {
        m_backend.report(QStringLiteral("app %1 is not a view module in this Bundled set")
                             .arg(name));
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
        m_backend.report(QStringLiteral("app %1 did not come up").arg(name));
        delete runner;
        delete surface;
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
    // Closing a Downloaded app is the z-order instruction in reverse, and it is
    // NOT an unload: the module stays loaded and keeps answering calls, its page
    // simply stops covering the Shell. Nothing is destroyed behind the
    // container's back.
    if (m_backend.isDownloadedApp(name)) {
        basecamp::web::MobileWebContainerBackend::instance()->hideAll();
        if (m_backend.currentVisibleApp() == name)
            m_backend.setCurrentVisibleApp(QString());
        m_backend.report(QStringLiteral("downloaded app %1 is off screen and still "
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
