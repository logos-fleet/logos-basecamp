// The Modules-tab acceptance criterion as a round trip, driven from inside the
// app.
//
// The presses are synthesised (ShellSceneDriver), and only the touch is:
// everything after it is the real path -- the Shell's own LogosButton, its
// MouseArea, ModuleInspectorView's loadRequested / unloadRequested, ContentViews'
// backend call, ShellModulesBackend, CoreModuleManager, ICoreRuntime, and the
// Native container underneath. That is the difference between "the Modules tab
// works" and "the C API works", and it is why this drives the BUTTON and reads
// the ROW back rather than asking the runtime twice.
//
// Everything is found by objectName, the handles the views already carry for
// UI automation ("moduleInspector.table", "moduleRow.loadToggle.<name>",
// "settings.section.<key>"). No host type is named on the QML side.
#pragma once

#include "ShellSceneDriver.h"

class BundledSetShellHost;

class ShellModulesDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellModulesDriver(BundledSetShellHost* host, QWidget* shellWidget,
                       QObject* parent = nullptr);

    // Opens Settings -> Module Inspector, checks the rows against the Bundled
    // set, and presses Load/Unload twice on every row the core is in charge of
    // -- every one of them, because the modules that own threads of their own
    // are never the first (#96). Runs after the first frame: a QML item has no
    // geometry before one, so there is no button to press.
    void run();

private:
    // Settings -> Apps Inspector, before anything is loaded or unloaded.
    //
    // THE OTHER PANE (logos-workspace#146). Settings draws two inspectors and
    // the acceptance pass only ever read one of them, so nothing noticed that
    // on a phone the Apps one was EMPTY: `uiModulesModel` was null, a `web` app
    // was listed under Modules and nowhere else, and the same Shell was drawing
    // it a sidebar tile and mounting it in the dock.
    //
    // Read off the SCENE and compared with the TILES, for the reason the
    // Modules check is: the two panes are two answers to "what is an app", and
    // the sidebar is the third. A model compared with itself would prove only
    // that this host can copy a list.
    //
    // Read-only, and first: everything after it changes what is loaded.
    void checkAppsInspector();

    // The three things outside the core that unloading a `web` app has to
    // move: its page, the Shell's tab onto it, and what its sidebar tile
    // claims. Reported as one line when they all hold, and named one at a time
    // when one does not.
    void checkUnloadedWebApp(const QString& name);

    BundledSetShellHost* m_host;  // not owned
};
