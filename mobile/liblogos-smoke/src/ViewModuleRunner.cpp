#include "ViewModuleRunner.h"

#include <logos_api.h>
#include <logos_protocol.h>

#include <LogosViewPlugin.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRemoteObjectHost>
#include <QRemoteObjectNode>
#include <QRemoteObjectReplica>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <sys/socket.h>

namespace {

// The view framework's C edge — logos-module-builder's
// cmake/LogosViewFrameworkAbi.cpp.in. Declared here as function POINTER types
// because that is how they are reached: dlsym, never a link.
using AbiVersionFn = unsigned (*)();
using StringFn = const char* (*)();
using CreateFn = void* (*)();
using AcquireReplicaFn = void* (*)(void*);

// What this host knows how to drive. The framework reports its own number and
// a mismatch is refused rather than called into: the entry points below are
// resolved by NAME, so an image from a later builder would answer a different
// question with the same signature.
constexpr unsigned kSupportedViewAbi = 1;

// The bundle-relative path Xcode's "Embed Frameworks" phase writes. The stem
// is the module's, and comes from the build (LOGOS_VIEW_MODULE_STEM) rather
// than being spelled here: it is the same string the nix expression uses to
// find the framework to embed.
#ifndef LOGOS_VIEW_MODULE_STEM
#  error "LOGOS_VIEW_MODULE_STEM must be defined by the build (see stage/CMakeLists.txt)"
#endif

QString viewImageSuffix()
{
    const QString stem = QStringLiteral(LOGOS_VIEW_MODULE_STEM);
    return QStringLiteral("/Frameworks/%1.framework/%1").arg(stem);
}

// Same question, same answer, as BundledModuleRunner: the directory the Logos
// code is ACTUALLY loaded from, asked of the dynamic loader.
QString logosImageDir()
{
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&lp_protocol_version), &info) == 0 || !info.dli_fname)
        return {};
    return QFileInfo(QString::fromUtf8(info.dli_fname)).absolutePath();
}

template <typename Fn>
Fn resolve(void* handle, const char* name)
{
    return reinterpret_cast<Fn>(dlsym(handle, name));
}

} // namespace

QObject* InProcViewBridge::module(const QString& name) const
{
    return name == m_name ? m_replica : nullptr;
}

void InProcViewBridge::publish(const QString& name, QObject* replica)
{
    m_name = name;
    m_replica = replica;
}

ViewModuleRunner::ViewModuleRunner(QObject* parent)
    : QObject(parent)
{
}

ViewModuleRunner::~ViewModuleRunner()
{
    // The image is deliberately NOT dlclose()d. The replica, the node and the
    // plugin object all hold code and vtables that live in it, and unmapping
    // running code is the one failure mode that does not name itself.
    delete m_node;
    delete m_host;
    delete m_plugin;
    delete m_api;
}

QString ViewModuleRunner::viewImagePath()
{
    const QString dir = logosImageDir();
    if (dir.isEmpty())
        return {};
    return dir + viewImageSuffix();
}

bool ViewModuleRunner::run(QQuickWidget* surface)
{
    if (!surface) {
        emit log(QStringLiteral("view module: no surface to render into"));
        return false;
    }

    const QString imagePath = viewImagePath();
    if (imagePath.isEmpty()) {
        emit log(QStringLiteral("view module: could not locate the Logos image directory"));
        return false;
    }
    emit log(QStringLiteral("view image: %1").arg(imagePath));
    if (!QFileInfo::exists(imagePath)) {
        emit log(QStringLiteral("view module: no framework at that path (embed step did not run?)"));
        return false;
    }

    // RTLD_LOCAL: the module's symbols stay out of the global namespace, so a
    // second view module with the same generated names cannot shadow this one
    // (spike ios-dlopen-bare-module, Level 3). Timed because the dlopen is
    // where the image's qrc static initializer runs — this number is the
    // module's code AND its QML arriving in the process.
    QElapsedTimer t;
    t.start();
    ::dlerror();
    m_handle = dlopen(imagePath.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
    const qint64 dlopenMs = t.elapsed();
    if (!m_handle) {
        const char* why = ::dlerror();
        emit log(QStringLiteral("view module: dlopen failed -- %1")
                     .arg(why ? QString::fromUtf8(why) : QStringLiteral("no reason given")));
        return false;
    }
    emit log(QStringLiteral("view image opened in %1 ms").arg(dlopenMs));

    auto abiVersion = resolve<AbiVersionFn>(m_handle, "logos_view_module_abi_version");
    auto moduleName = resolve<StringFn>(m_handle, "logos_view_module_name");
    auto qmlUrlFn = resolve<StringFn>(m_handle, "logos_view_module_qml_url");
    auto create = resolve<CreateFn>(m_handle, "logos_view_module_create");
    auto acquire = resolve<AcquireReplicaFn>(m_handle, "logos_view_module_acquire_replica");
    if (!abiVersion || !moduleName || !qmlUrlFn || !create || !acquire) {
        emit log(QStringLiteral("view module: the image does not carry the view ABI"));
        return false;
    }
    if (abiVersion() != kSupportedViewAbi) {
        emit log(QStringLiteral("view module: ABI %1, this host drives %2")
                     .arg(abiVersion()).arg(kSupportedViewAbi));
        return false;
    }

    const QString name = QString::fromUtf8(moduleName());
    const QString qmlUrl = QString::fromUtf8(qmlUrlFn());
    emit log(QStringLiteral("view module: %1, view at %2").arg(name, qmlUrl));

    // The plugin object, and the backend behind it. `initLogos` is invoked by
    // REFLECTION for the same reason ui-host does it: the host compiles
    // against no module header, so the only thing it knows about the class is
    // what its meta-object says.
    m_plugin = static_cast<QObject*>(create());
    if (!m_plugin) {
        emit log(QStringLiteral("view module: the image returned no plugin object"));
        return false;
    }
    m_api = new LogosAPI(name);
    if (m_plugin->metaObject()->indexOfMethod("initLogos(LogosAPI*)") != -1) {
        QMetaObject::invokeMethod(m_plugin, "initLogos", Qt::DirectConnection,
                                  Q_ARG(LogosAPI*, m_api));
    }

    // Both casts, because either can be the one that works. qobject_cast finds
    // the interface only if the plugin class named it in Q_INTERFACES (what
    // logos-qt-sdk's ui generator emits); a hand-written plugin that just
    // inherits the generated base needs the C++ cast. And LogosViewPlugin is
    // declared twice -- once in this host, once inside the framework -- so the
    // dynamic_cast is matching typeinfo across the dlopen boundary by name.
    auto* viewPlugin = qobject_cast<LogosViewPlugin*>(m_plugin);
    if (!viewPlugin)
        viewPlugin = dynamic_cast<LogosViewPlugin*>(m_plugin);
    if (!viewPlugin) {
        emit log(QStringLiteral("view module: the plugin does not implement LogosViewPlugin"));
        return false;
    }

    // ── the in-process replica path ─────────────────────────────────────────
    // The same typed seam the desktop carries over a ui-host subprocess, with
    // both ends in this process. It is NOT collapsed to "hand the QML the
    // backend object directly": that would give the phone a different object
    // model from the desktop — no replica lifecycle, no Valid state, and
    // properties that update synchronously — so a module that worked here
    // could still be wrong in Basecamp.
    //
    // THE TRANSPORT IS A socketpair(), not `local:<name>`, and on a phone it
    // has to be. A QLocalServer is a filesystem socket, and `sun_path` is 104
    // bytes on Darwin while an iOS app's own tmp directory is longer than that
    // before a name is appended -- measured on the simulator, where
    // QRemoteObjectHost answers `Listen failed for URL: QUrl("local:...")` /
    // HostNotFoundError. A socketpair has no path at all. It is also the
    // truer shape: these two ends are in ONE process and never wanted a name
    // anything else could connect to.
    int fds[2] = { -1, -1 };
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        emit log(QStringLiteral("view module: socketpair failed (%1)")
                     .arg(QString::fromUtf8(strerror(errno))));
        return false;
    }
    auto* hostSide = new QLocalSocket(this);
    auto* clientSide = new QLocalSocket(this);
    if (!hostSide->setSocketDescriptor(fds[0], QLocalSocket::ConnectedState)
        || !clientSide->setSocketDescriptor(fds[1], QLocalSocket::ConnectedState)) {
        emit log(QStringLiteral("view module: could not adopt the socketpair"));
        return false;
    }

    m_host = new QRemoteObjectHost;
    // AllowExternalRegistration is what makes a host with no server of its own
    // legal. A default-constructed QRemoteObjectHost has no QRemoteObjectSourceIo
    // at all, and enableRemoting() walks into it: measured as a SIGSEGV at
    // KERN_INVALID_ADDRESS 0x30 inside QRemoteObjectSourceIo::enableRemoting,
    // before the first frame. Setting the URL with this flag builds the source
    // side WITHOUT trying to listen -- which is exactly the arrangement here,
    // since the connection arrives ready-made below. The scheme is deliberately
    // not `local:`: nothing can connect to this name.
    m_host->setHostUrl(QUrl(QStringLiteral("exsocketpair:logos-view-") + name),
                       QRemoteObjectHost::AllowExternalRegistration);
    if (!viewPlugin->enableRemoting(m_host)) {
        emit log(QStringLiteral("view module: enableRemoting refused the backend"));
        return false;
    }
    m_host->addHostSideConnection(hostSide);
    m_node = new QRemoteObjectNode;
    m_node->addClientSideConnection(clientSide);
    m_replica = static_cast<QObject*>(acquire(m_node));
    if (!m_replica) {
        emit log(QStringLiteral("view module: the image returned no replica"));
        return false;
    }
    auto* replica = qobject_cast<QRemoteObjectReplica*>(m_replica);
    if (replica && !replica->waitForSource(5000)) {
        emit log(QStringLiteral("view module: the replica never reached the source"));
        return false;
    }
    emit log(QStringLiteral("replica valid: %1").arg(name));

    // ── the view, in the HOST's engine ──────────────────────────────────────
    // Loaded straight out of the framework's qrc, which the dlopen above
    // registered. AC: this number is what the spike measured at 14 ms on
    // device, 8 ms on the simulator.
    m_bridge = new InProcViewBridge(this);
    m_bridge->publish(name, m_replica);
    surface->engine()->rootContext()->setContextProperty(QStringLiteral("logos"), m_bridge);

    QElapsedTimer qmlTimer;
    qmlTimer.start();
    surface->setSource(QUrl(qmlUrl));
    const qint64 qmlMs = qmlTimer.elapsed();
    if (surface->status() == QQuickWidget::Error) {
        for (const QQmlError& e : surface->errors())
            emit log(QStringLiteral("qml: %1").arg(e.toString()));
        emit log(QStringLiteral("view module: the QML did not load"));
        return false;
    }
    emit log(QStringLiteral("QML loaded from the framework's resources in %1 ms").arg(qmlMs));
    m_surface = surface;
    emit log(QStringLiteral("BUNDLED VIEW MODULE OK"));
    return true;
}

void ViewModuleRunner::driveViewOnce()
{
    if (!m_surface || !m_replica) {
        emit log(QStringLiteral("drive: nothing loaded to drive"));
        return;
    }
    QQuickItem* root = m_surface->rootObject();
    auto* button = root ? root->findChild<QQuickItem*>(QStringLiteral("incrementButton")) : nullptr;
    auto* label = root ? root->findChild<QQuickItem*>(QStringLiteral("countLabel")) : nullptr;
    if (!button || !label) {
        emit log(QStringLiteral("drive: the view has no incrementButton/countLabel"));
        return;
    }

    const int before = m_replica->property("count").toInt();
    const QString labelBefore = label->property("text").toString();

    // Scene coordinates ARE widget coordinates for a QQuickWidget, so the
    // centre of the button in the scene is where the press goes.
    const QPointF centre = button->mapToScene(
        QPointF(button->width() / 2.0, button->height() / 2.0));
    const QPointF global = m_surface->mapToGlobal(centre);
    QMouseEvent press(QEvent::MouseButtonPress, centre, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QGuiApplication::sendEvent(m_surface, &press);
    QGuiApplication::sendEvent(m_surface, &release);

    // The round trip is asynchronous in BOTH directions across the node, so
    // wait for the property to arrive rather than reading it back straight
    // away -- which would report the old value and call it a failure.
    QElapsedTimer rt;
    rt.start();
    while (m_replica->property("count").toInt() == before && rt.elapsed() < 3000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    const int after = m_replica->property("count").toInt();
    const QString labelAfter = label->property("text").toString();
    const QString status = m_replica->property("status").toString();
    if (after != before + 1) {
        emit log(QStringLiteral("drive: FAIL -- the button did not reach the backend "
                                "(count stayed %1)").arg(before));
        return;
    }
    if (labelAfter == labelBefore) {
        emit log(QStringLiteral("drive: FAIL -- the backend changed to %1 and the view "
                                "still reads '%2'").arg(after).arg(labelAfter));
        return;
    }
    emit log(QStringLiteral("drive: tapped Increment -- backend %1 -> %2, status '%3', "
                            "label '%4' -> '%5' in %6 ms")
                 .arg(before).arg(after).arg(status, labelBefore, labelAfter)
                 .arg(rt.elapsed()));
    emit log(QStringLiteral("VIEW ROUND TRIP OK"));
}
