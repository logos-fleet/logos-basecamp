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
class WebAppSurface;

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

    // The placeholder a WEB app is docked as, or nullptr. There is no widget to
    // return for one -- its UI is a platform page -- so this is the next best
    // thing a driver can assert on: the hole the Shell left for the page, and
    // where the Shell put it. See WebAppSurface.h.
    WebAppSurface* webSurface(const QString& name) const;

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

    // Dock a placeholder for a web app and follow it. A `web` module has no
    // widget, so the Shell had nothing to dock and its page was raised over the
    // whole window -- which covered the Shell's own navigation and left the
    // user inside an app with no way out (#110). The placeholder is what makes
    // a web app navigate like every other one: same dock, same tab, same close
    // button, and the page inset to the hole the workspace left.
    void mountWebApp(const QString& name);

    // ONE PLACE DECIDES WHICH PAGE IS UP, on the turn after the Shell has laid
    // out. Deferred because a tab switch shows the incoming placeholder before
    // it hides the outgoing one, and a host that answered each event as it
    // arrived would send the page it had just raised straight back down.
    void syncWebSurfaces();
    void queueWebSync();

    struct Mounted {
        QQuickWidget*     widget = nullptr;   // owned by the Shell once handed over
        ViewModuleRunner* runner = nullptr;   // owned by this
    };

    ShellModulesBackend             m_backend;
    IShellObserver*                 m_observer = nullptr;
    QHash<QString, Mounted>         m_mounted;
    // The placeholders, by module. Owned by the Shell once handed over, exactly
    // as a `ui_qml` widget is.
    QHash<QString, WebAppSurface*>  m_webSurfaces;
    // The web module whose page is currently in front, so a placement that did
    // not change which app is up does not re-spend the live-runtime budget.
    QString                         m_webVisible;
    bool                            m_webSyncQueued = false;
};
