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
    // set, and presses one row's Load/Unload twice. Runs after the first frame:
    // a QML item has no geometry before one, so there is no button to press.
    void run();

private:
    BundledSetShellHost* m_host;  // not owned
};
