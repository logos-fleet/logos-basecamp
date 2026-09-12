#include "webview/MobileWebContainerBackend.h"

#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <utility>

namespace basecamp::web {

namespace {

QString megabytes(qint64 bytes)
{
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 0);
}

} // namespace

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

    auto* view = new MobileWebModuleView(request, runtimeDir, m_platform);
    if (!view->startupError().isEmpty()) {
        delete view;
        return nullptr;
    }

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
                                        const LiveRuntimeBudget& budget)
{
    m_platform = std::move(platform);
    m_budget = budget;

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

QStringList MobileWebContainerBackend::loadedModules() const
{
    QStringList names = m_views.keys();
    names.sort();
    return names;
}

QStringList MobileWebContainerBackend::show(const QString& moduleName)
{
    const QStringList evicted = m_budget.show(moduleName);

    qInfo().noquote()
        << QStringLiteral("Web container: %1 is visible; %2 live runtime(s), %3 of %4")
               .arg(moduleName, QString::number(m_budget.live().size()),
                    megabytes(m_budget.projectedBytes()),
                    megabytes(m_budget.budgetBytes()));

    for (const QString& name : evicted) {
        qInfo().noquote()
            << QStringLiteral("Web container: over budget -- %1 gives up its UI page "
                              "(%2 reclaimed)")
                   .arg(name, megabytes(m_budget.runtimeFootprintBytes()));
        emit uiEvictionRequired(name);
    }
    return evicted;
}

} // namespace basecamp::web
