// IShellHost, implemented over a Bundled set.
//
// The Shell is handed one IShellHost* and nothing else -- no LogosAPI, no core
// handle -- and that contract is unchanged here (IShellHost.h). Which is the
// point of this file being small: the same main_ui archive the desktop plugin
// is built from runs on a phone because the phone can answer these eight
// methods, not because anything about the Shell was made mobile.
#pragma once

#include "IShellHost.h"
#include "ShellModulesBackend.h"

#include <QHash>

class BundledSetCoreRuntime;
class QQuickWidget;
class ViewModuleRunner;

class BundledSetShellHost : public IShellHost
{
public:
    explicit BundledSetShellHost(BundledSetCoreRuntime* core);
    ~BundledSetShellHost() override;

    QObject* backendObject() override;
    int      currentSectionIndex() const override;
    void     setCurrentSectionIndex(int index) override;
    void     loadUiModule(const QString& name) override;
    void     unloadUiModule(const QString& name) override;
    void     setCurrentVisibleApp(const QString& name) override;
    QString  displayNameFor(const QString& name) const override;
    void     setObserver(IShellObserver* observer) override;

    // Direct access for the host's own startup and for the acceptance pass.
    // Not part of IShellHost: the Shell must not be able to reach it.
    ShellModulesBackend* backend() { return &m_backend; }

    // Announce the current section to the observer. The shell opens on the
    // workspace and only moves on a callback, so a section chosen before the
    // shell existed -- which is every section this host sets at startup --
    // needs saying once the observer is attached.
    void replaySection();

    // The widget a mounted app renders into, or nullptr. For a driver that has
    // to assert the app reached the screen rather than only that the mount
    // returned true.
    QQuickWidget* mountedView(const QString& name) const;

private:
    // Bring a `ui_qml` member of the Bundled set up and hand its widget to the
    // Shell: dlopen the framework, instantiate the backend in THIS process,
    // acquire the typed replica, load the module's QML out of the image's qrc
    // (all ViewModuleRunner's, shared with the smoke probe), then hand the
    // widget over through onPluginWindowRequested -- the same callback the
    // desktop host uses, so the Shell mounts a phone's app exactly as it
    // mounts a desktop one.
    //
    // A second call for a mounted app presents it rather than mounting a
    // second copy: a sidebar tile is a toggle.
    void mountApp(const QString& name);
    void unmountApp(const QString& name);

    struct Mounted {
        QQuickWidget*     widget = nullptr;   // owned by the Shell once handed over
        ViewModuleRunner* runner = nullptr;   // owned by this
    };

    ShellModulesBackend      m_backend;
    IShellObserver*          m_observer = nullptr;
    QHash<QString, Mounted>  m_mounted;
};
