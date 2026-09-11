// Brings the app's ONE Bundled VIEW module up inside the running core and
// hands its QML to the host's own engine.
//
// A view module is a `type: ui_qml` module: a Qt backend generated from a
// .rep plus a QML file. On the desktop the backend runs in a `ui-host`
// SUBPROCESS and the host talks to it over a local socket. A phone has no
// subprocess a store will accept (ADR 0003), so here the backend is
// instantiated IN THIS PROCESS and the same typed-replica seam is carried
// over a node in the same process. Everything above the replica — the QML,
// the property bindings, the slot calls — is unchanged, which is the point:
// one Main.qml, two hosts.
//
//   the image     <App>.app/Frameworks/<name>_view.framework/<name>_view,
//                 embedded by Xcode with Code Sign On Copy, Qt and LogosAPI
//                 bound upward into the app (ADR 0006, logos-module-builder's
//                 `view` output).
//   the edge      six C functions, reached by dlsym and nothing else; there is
//                 no plugin directory on a phone to scan.
//   the QML       inside the image's own qrc, so the engine loads it out of
//                 the module's bytes and never touches the filesystem.
#pragma once

#include <QObject>
#include <QString>

class QQuickWidget;
class QRemoteObjectHost;
class QRemoteObjectNode;
class LogosAPI;

// What the QML calls `logos`. One member, `module(name)`, which is the only
// part of the desktop bridge a view module's own QML uses to reach its
// backend — so the module's Main.qml is byte-identical on both hosts.
//
// It is NOT logos-view-module-runtime's LogosQmlBridge: callModule, the
// intent broker and the model plumbing are the shell's, and this host is a
// bring-up probe with one module in it.
class InProcViewBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE QObject* module(const QString& name) const;

    void publish(const QString& name, QObject* replica);

private:
    QString m_name;
    QObject* m_replica = nullptr;
};

class ViewModuleRunner : public QObject
{
    Q_OBJECT
public:
    explicit ViewModuleRunner(QObject* parent = nullptr);
    ~ViewModuleRunner() override;

    // dlopen the framework, construct the plugin, publish its typed source on
    // an in-process node, acquire the replica and load the module's QML from
    // the image's qrc into `surface`'s engine. Says why through log() on
    // every failure.
    //
    // Must run after logos_core_start().
    bool run(QQuickWidget* surface);

    // Where the framework is, resolved from the running process rather than
    // guessed — see BundledModuleRunner::bundledImagePath() for why dladdr
    // and not applicationDirPath().
    static QString viewImagePath();

    // The replica the QML is bound to, for a driver that wants to read the
    // backend's state without going through the view.
    QObject* replica() const { return m_replica; }

    // Press the view's own button and watch the number come back.
    //
    // This is the acceptance criterion as a round trip, driven from inside the
    // app because a simulator has no tap to send: a synthetic press/release at
    // the button's centre enters the scene exactly where a finger's would, and
    // from there everything is the real path -- the QML MouseArea calls
    // increment() on the REPLICA, the call crosses to the source in the
    // framework, the source sets its property, the change crosses back, and
    // the binding rewrites the label. Nothing about it is stubbed; the only
    // thing skipped is UIKit's delivery of the touch.
    //
    // Runs after the first frame (the scene has no geometry before it) and
    // reports through log().
    void driveViewOnce();

signals:
    void log(const QString& line);

private:
    void* m_handle = nullptr;
    LogosAPI* m_api = nullptr;
    QObject* m_plugin = nullptr;
    QRemoteObjectHost* m_host = nullptr;
    QRemoteObjectNode* m_node = nullptr;
    QObject* m_replica = nullptr;
    InProcViewBridge* m_bridge = nullptr;
    QQuickWidget* m_surface = nullptr;
};
