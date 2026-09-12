#include "web/WebContainerBackend.h"
#include "web/WebModulePageView.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

namespace basecamp::web {

WebContainerBackend* WebContainerBackend::instance()
{
    static WebContainerBackend backend;
    return &backend;
}

QString WebContainerBackend::logosQmlRuntimeDir()
{
    const auto usable = [](const QString& dir) {
        return !dir.isEmpty()
               && QFileInfo(dir).isDir()
               && QFileInfo(QDir(dir).filePath(QStringLiteral("logos_qml_runtime.js"))).isFile();
    };

    const QString fromEnv = qEnvironmentVariable("LOGOS_QML_RUNTIME_DIR");
    if (!fromEnv.isEmpty()) {
        if (usable(fromEnv)) return QDir(fromEnv).absolutePath();
        qWarning() << "LOGOS_QML_RUNTIME_DIR points at" << fromEnv
                   << "which holds no logos_qml_runtime.js; ignoring it";
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) return {};
    for (const char* relative : { "../Resources/logos-runtime",
                                  "../share/logos-runtime",
                                  "logos-runtime" }) {
        const QString candidate = QDir(appDir).filePath(QLatin1String(relative));
        if (usable(candidate)) return QDir(candidate).absolutePath();
    }
    return {};
}

WebModulePageView* WebContainerBackend::createView(const LogosCore::WebModuleViewRequest& request,
                                                   const QString& runtimeDir)
{
    const QString name = QString::fromStdString(request.moduleName);

    auto* view = new WebModulePageView(request, runtimeDir);
    if (!view->startupError().isEmpty() || !view->widget()) {
        delete view;
        return nullptr;
    }

    m_views.insert(name, view);
    // The view is the container's, so the container's destruction is what takes
    // the entry out: a QPointer that has gone null is a module whose page the
    // container already destroyed, and the shell has to hear about it.
    connect(view, &QObject::destroyed, this, [this, name] {
        if (!m_views.contains(name)) return;
        m_views.remove(name);
        emit viewClosed(name);
    });

    emit viewOpened(name, view->widget());
    return view;
}

void WebContainerBackend::install(const QString& runtimeDir)
{
    if (runtimeDir.isEmpty()) {
        qWarning() << "Web container: this build ships no bundled QML runtime. A `web` "
                      "variant whose manifest asks for one will load its page and fail "
                      "there; set LOGOS_QML_RUNTIME_DIR to a runtime's www directory.";
    } else {
        qInfo() << "Web container: bundled QML runtime at" << runtimeDir;
    }

    // The thread install() was called on — see the header: a view is a widget,
    // so it may only be built here, whatever thread the core loads from.
    QThread* const uiThread = QThread::currentThread();
    LogosCore::setWebModuleViewFactory(
        [this, runtimeDir, uiThread](const LogosCore::WebModuleViewRequest& request)
            -> std::unique_ptr<LogosCore::WebModuleView> {
            WebModulePageView* view = nullptr;
            if (QThread::currentThread() == uiThread) {
                view = createView(request, runtimeDir);
            } else {
                // BLOCKING, and it is the load path that makes it safe: the core
                // is asking this thread for a view and will not proceed without
                // one, so there is nothing for the UI thread to be waiting on in
                // return. A queued-and-poll would be the same wait with the
                // answer arriving later.
                QMetaObject::invokeMethod(
                    this, [this, &request, &runtimeDir, &view] {
                        view = createView(request, runtimeDir);
                    },
                    Qt::BlockingQueuedConnection);
            }
            return std::unique_ptr<LogosCore::WebModuleView>(view);
        });
}

QWidget* WebContainerBackend::widgetFor(const QString& moduleName) const
{
    const QPointer<WebModulePageView> view = m_views.value(moduleName);
    return view ? view->widget() : nullptr;
}

bool WebContainerBackend::hasView(const QString& moduleName) const
{
    return !m_views.value(moduleName).isNull();
}

} // namespace basecamp::web
