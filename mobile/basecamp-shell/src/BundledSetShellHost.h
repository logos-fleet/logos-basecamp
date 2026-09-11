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

class BundledSetCoreRuntime;

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

private:
    ShellModulesBackend m_backend;
    IShellObserver*     m_observer = nullptr;
};
