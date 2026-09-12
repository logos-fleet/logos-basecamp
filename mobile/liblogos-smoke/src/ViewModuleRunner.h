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

#include <QHash>
#include <QObject>
#include <QString>

class QQuickWidget;
class QRemoteObjectHost;
class QRemoteObjectNode;
class LogosAPI;

// What the QML calls `logos`. Two members, `module(name)` and
// `model(module, name)`: the parts of the desktop bridge a view module's own
// QML uses to reach its backend and its list models, so the module's QML is
// byte-identical on both hosts.
//
// It is NOT logos-view-module-runtime's LogosQmlBridge: callModule, the intent
// broker and the cross-module plumbing are the shell's. What IS the same is
// the model protocol, because it has to be -- a remoted QAbstractItemModel is
// acquired by the name `<module>/<property>` on the node, and QML asking
// `logos.model("chat_ui", "conversationModel")` is asking both hosts the same
// question.
class InProcViewBridge : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    Q_INVOKABLE QObject* module(const QString& name) const;

    // The module's remoted list model, acquired on first ask and kept. Same
    // signature and same default as LogosQmlBridge::model, so a view module's
    // QML never learns which host it is in.
    Q_INVOKABLE QObject* model(const QString& moduleName, const QString& modelName,
                               bool prefetch = true);

    void publish(const QString& name, QObject* replica, QRemoteObjectNode* node);

private:
    QString m_name;
    QObject* m_replica = nullptr;
    QRemoteObjectNode* m_node = nullptr;  // not owned
    QHash<QString, QObject*> m_models;
};

class ViewModuleRunner : public QObject
{
    Q_OBJECT
public:
    // The framework this runner opens, as the stem its bundle is named after
    // ("<module>_view"). The default is the ONE view module the build resolved
    // out of the Bundled set (LOGOS_VIEW_MODULE_STEM) -- what the smoke probe
    // renders. The Shell passes a stem instead, because there the set may
    // carry several and which one is mounted is a user's choice, not a build's.
    explicit ViewModuleRunner(QObject* parent = nullptr);
    explicit ViewModuleRunner(QString stem, QObject* parent = nullptr);
    ~ViewModuleRunner() override;

    // dlopen the framework, construct the plugin, publish its typed source on
    // an in-process node, acquire the replica and load the module's QML from
    // the image's qrc into `surface`'s engine. Says why through log() on
    // every failure.
    //
    // Must run after logos_core_start().
    bool run(QQuickWidget* surface);

    // Where the framework with this stem is, resolved from the running process
    // rather than guessed — see BundledModuleRunner::bundledImagePath() for
    // why dladdr and not applicationDirPath().
    static QString viewImagePathFor(const QString& stem);

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
    // Remote every Q_PROPERTY on the plugin whose value is a
    // QAbstractItemModel* as a child source named `<module>/<property>`, which
    // is what the bridge's model() acquires. The same scan ui-host does on the
    // desktop, and it has to be here too: enableRemoting() on the .rep source
    // carries properties, signals and slots, and NOT the models hanging off it.
    // `target` is the plugin's viewObject() -- the backend QML binds to, which
    // is where a ui_qml module's models live.
    void remoteModelProperties(const QString& name, QObject* target);

    QString m_stem;
    void* m_handle = nullptr;
    LogosAPI* m_api = nullptr;
    QObject* m_plugin = nullptr;
    QRemoteObjectHost* m_host = nullptr;
    QRemoteObjectNode* m_node = nullptr;
    QObject* m_replica = nullptr;
    InProcViewBridge* m_bridge = nullptr;
    QQuickWidget* m_surface = nullptr;
};
