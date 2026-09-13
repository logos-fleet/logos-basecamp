#include "webview/MobileWebContainerBackend.h"

#include "webview/AppMemory.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <utility>

namespace basecamp::web {

namespace {

// A directory holding the runtime's glue script IS the runtime; anything else
// named as one is a misconfiguration, and saying so beats a page that comes up
// and then cannot find what it was promised.
bool holdsQmlRuntime(const QString& dir)
{
    return !dir.isEmpty()
           && QFileInfo(dir).isDir()
           && QFileInfo(QDir(dir).filePath(QStringLiteral("logos_qml_runtime.js"))).isFile();
}

} // namespace

QString bundledQmlRuntimeDir(const QString& platformDir)
{
    const QString fromEnv = qEnvironmentVariable("LOGOS_QML_RUNTIME_DIR");
    if (!fromEnv.isEmpty()) {
        if (holdsQmlRuntime(fromEnv)) return QDir(fromEnv).absolutePath();
        qWarning() << "LOGOS_QML_RUNTIME_DIR points at" << fromEnv
                   << "which holds no logos_qml_runtime.js; ignoring it";
    }
    return holdsQmlRuntime(platformDir) ? QDir(platformDir).absolutePath() : QString();
}

MobileWebContainerBackend* MobileWebContainerBackend::instance()
{
    static MobileWebContainerBackend backend;
    return &backend;
}

void MobileWebContainerBackend::armPollTimer()
{
    if (m_pollTimer) return;
    // A long poll the host never completes is a fetch the page waits on
    // forever; the page re-arms the moment one settles, so an idle module costs
    // one request every kPollParkMs and nothing in between.
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(MobileWebBridge::kPollParkMs);
    connect(m_pollTimer, &QTimer::timeout, this, [this] {
        for (MobileWebModuleView* view : m_views) view->expireWaits();
    });
    m_pollTimer->start();
}

MobileWebModuleView* MobileWebContainerBackend::createView(
    const LogosCore::WebModuleViewRequest& request, const QString& runtimeDir)
{
    const QString name = QString::fromStdString(request.moduleName);

    auto* view = new MobileWebModuleView(request, runtimeDir, m_platform, m_shimInDocument,
                                        m_origin);
    if (!view->startupError().isEmpty()) {
        delete view;
        return nullptr;
    }

    // THE PAGE'S CONSOLE, ANNOUNCED. Wired before the registry entry and before
    // anyone is told the view exists, so a host that wants to wait for a line
    // the view will print cannot miss it: the webview's first request has not
    // been served yet -- both platforms load asynchronously.
    // QUEUED, for the reason WebPageProbe's sink is: Android calls
    // `shouldInterceptRequest` on a Chromium background thread, so a page's
    // console line arrives on it -- and a shell connected to this signal appends
    // to a widget. Qt Widgets from a second thread is an immediate SIGSEGV.
    view->setOnPageLog([this, name](const QString& level, const QString& message) {
        QMetaObject::invokeMethod(this, [this, name, level, message] {
            emit pageLog(name, level, message);
        }, Qt::QueuedConnection);
    });

    m_views.insert(name, view);
    // The view is the CONTAINER's, so the container's destruction is what takes
    // the entry out: anything else would leave the registry naming a page that
    // no longer exists, and the shell would mount a dangling handle.
    view->setOnDestroyed([this, name] { forget(name); });
    armPollTimer();
    emit viewOpened(name, view->nativeHandle());
    return view;
}

void MobileWebContainerBackend::forget(const QString& moduleName)
{
    if (!m_views.remove(moduleName)) return;
    m_budget.forget(moduleName);
    emit viewClosed(moduleName);
}

void MobileWebContainerBackend::install(const QString& runtimeDir,
                                        PlatformPageFactory platform,
                                        const LiveRuntimeBudget& budget,
                                        bool shimInDocument,
                                        WebOrigin origin)
{
    m_platform = std::move(platform);
    m_budget = budget;
    m_shimInDocument = shimInDocument;
    m_origin = std::move(origin);

    if (runtimeDir.isEmpty()) {
        qWarning() << "Web container: this build ships no bundled QML runtime. A `web` "
                      "variant whose loader asks for one will load its page and fail "
                      "there.";
    } else {
        qInfo() << "Web container: bundled QML runtime at" << runtimeDir;
    }
    qInfo().noquote() << QStringLiteral("Web container: live-runtime budget %1 "
                                        "(%2 runtime%3 x %4)")
                             .arg(megabytes(m_budget.budgetBytes()),
                                  QString::number(m_budget.maxLiveRuntimes()),
                                  m_budget.maxLiveRuntimes() == 1 ? QString() : QStringLiteral("s"),
                                  megabytes(m_budget.runtimeFootprintBytes()));

    // The thread install() was called on — see the header: a webview may only be
    // built on the platform's UI thread, whatever thread the core loads from.
    QThread* const uiThread = QThread::currentThread();
    LogosCore::setWebModuleViewFactory(
        [this, runtimeDir, uiThread](const LogosCore::WebModuleViewRequest& request)
            -> std::unique_ptr<LogosCore::WebModuleView> {
            MobileWebModuleView* view = nullptr;
            if (QThread::currentThread() == uiThread) {
                view = createView(request, runtimeDir);
            } else {
                // BLOCKING, and it is the load path that makes it safe: the core
                // is asking this thread for a view and will not proceed without
                // one, so there is nothing for the UI thread to be waiting on in
                // return.
                QMetaObject::invokeMethod(
                    this, [this, &request, &runtimeDir, &view] {
                        view = createView(request, runtimeDir);
                    },
                    Qt::BlockingQueuedConnection);
            }
            return std::unique_ptr<LogosCore::WebModuleView>(view);
        });
}

void* MobileWebContainerBackend::nativeHandleFor(const QString& moduleName) const
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view ? view->nativeHandle() : nullptr;
}

bool MobileWebContainerBackend::hasView(const QString& moduleName) const
{
    return m_views.contains(moduleName);
}

bool MobileWebContainerBackend::hasUiPage(const QString& moduleName) const
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->hasUi();
}

void MobileWebContainerBackend::observeFramesFrom(const QString& moduleName,
                                                 std::function<void(const QString&)> sink)
{
    if (MobileWebModuleView* view = m_views.value(moduleName, nullptr))
        view->observeFrames(std::move(sink));
}

bool MobileWebContainerBackend::sendFrameTo(const QString& moduleName, const QString& frame)
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->sendFrame(frame);
}

bool MobileWebContainerBackend::runJavaScriptIn(const QString& moduleName,
                                                const QString& script)
{
    MobileWebModuleView* view = m_views.value(moduleName, nullptr);
    return view && view->runJavaScript(script);
}

QStringList MobileWebContainerBackend::loadedModules() const
{
    QStringList names = m_views.keys();
    names.sort();
    return names;
}

QString MobileWebContainerBackend::appMemoryLine(const QString& occasion)
{
    const qint64 bytes = appResidentBytes();
    return bytes < 0
        ? QStringLiteral("Web container: app memory %1: this platform does not say")
              .arg(occasion)
        : QStringLiteral("Web container: app memory %1: %2").arg(occasion, megabytes(bytes));
}

QStringList MobileWebContainerBackend::show(const QString& moduleName)
{
    const QStringList evicted = m_budget.show(moduleName);

    // THE SURFACE FOLLOWS THE BOOKS. A page is mounted behind the host's own
    // view, so a module the budget considers visible is still invisible until
    // its page is brought forward -- and the one that was in front has to go
    // back, or it would cover the new one whatever the budget thinks.
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it)
        it.value()->setFrontmost(it.key() == moduleName);

    qInfo().noquote()
        << QStringLiteral("Web container: %1 is visible; %2 live runtime(s), %3 of %4")
               .arg(moduleName, QString::number(m_budget.live().size()),
                    megabytes(m_budget.projectedBytes()),
                    megabytes(m_budget.budgetBytes()));
    qInfo().noquote() << appMemoryLine(QStringLiteral("with %1 visible").arg(moduleName));

    for (const QString& name : evicted) {
        MobileWebModuleView* view = m_views.value(name, nullptr);
        // THE MODULE STAYS, THE RUNTIME GOES. A variant with a headless entry
        // document is swapped onto it: same view, same bridge, same channel,
        // and the core is not told because from its side nothing happened.
        if (view && view->evictUi()) {
            qInfo().noquote()
                << QStringLiteral("Web container: over budget -- %1 gives up its UI page "
                                  "and keeps its Wasm host (%2 reclaimed)")
                       .arg(name, megabytes(m_budget.runtimeFootprintBytes()));
            emit uiEvicted(name);
            continue;
        }
        qInfo().noquote()
            << QStringLiteral("Web container: over budget -- %1 gives up its UI page "
                              "(%2 reclaimed); its package ships no headless document, "
                              "so it has to be unloaded")
                   .arg(name, megabytes(m_budget.runtimeFootprintBytes()));
        emit uiEvictionRequired(name);
    }

    // ...AND THE MODULE THE USER CHOSE GETS ITS UI BACK. Last, so that the page
    // coming up is the only live runtime at the moment it starts: a restore
    // before the eviction would put two of them in the same webview process for
    // the length of a load, which is the thing the budget exists to prevent.
    MobileWebModuleView* shown = m_views.value(moduleName, nullptr);
    if (shown && !shown->hasUi() && shown->restoreUi()) {
        shown->setFrontmost(true);
        qInfo().noquote()
            << QStringLiteral("Web container: %1 was in the background; its UI page is "
                              "coming back").arg(moduleName);
    }
    return evicted;
}

void MobileWebContainerBackend::hideAll()
{
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it)
        it.value()->setFrontmost(false);
    qInfo().noquote()
        << QStringLiteral("Web container: no module is visible; %1 live runtime(s) held")
               .arg(m_budget.live().size());
}

} // namespace basecamp::web
