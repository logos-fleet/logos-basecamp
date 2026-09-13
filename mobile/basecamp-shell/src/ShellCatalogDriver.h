// INSTALLING FROM A CATALOG, DRIVEN FROM INSIDE THE APP.
//
// The other two drivers exercise what the app SHIPS: the Modules tab lists the
// Bundled set (ShellModulesDriver), and a Bundled app opens from the sidebar
// (ShellAppDriver). This one exercises what the app does NOT ship -- a module
// that arrives at run time, out of a catalog, and ends up on screen in the
// workspace without the app being rebuilt.
//
// It is driven by a CatalogSource, i.e. by the command line, because the
// catalog a device is pointed at is not a property of the build. With nothing
// on the command line it has no work, exactly as ShellAppDriver has none for a
// Bundled set with no app in it.
//
// WHAT IT ASSERTS, in the order the user meets it:
//
//   1. the repository is added and the catalog comes back with rows
//   2. every row says whether it may be installed HERE, and a native-only one
//      says why not and offers no install control
//   3. the row's report and universal links are the ones the index published,
//      and the Shell will open them
//   4. Install -> the signer prompt -> approve -> the core discovers and loads
//      it (ShellModulesBackend::onModuleInstalled)
//   5. the sidebar carries a tile for it and pressing that tile puts its page
//      on screen
//
// Step 5 is the one that cannot be faked by any of the others: a module can
// install, load and open a page while the Shell still has no way to show it.
#pragma once

#include "ShellSceneDriver.h"
#include "appmanager/CatalogSource.h"

#include <QString>

class BundledSetShellHost;

class ShellCatalogDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellCatalogDriver(BundledSetShellHost* host, QWidget* shellWidget,
                       const basecamp::appmanager::CatalogSource& source,
                       QObject* parent = nullptr);

    // Whether this launch was pointed at a catalog at all.
    bool hasWork() const;

    // Add the repository and anchor the signers, then read the catalog. Safe to
    // call with no install requested -- browsing a catalog is most of what an
    // App Manager does.
    void configure();

    // Install the row the source named, and check it reached the workspace.
    // Runs after the first frame: pressing a tile needs one.
    void run();

    // Hand the row's two links to the platform (guideline 4.7.1 / 4.7.4).
    //
    // SEPARATE FROM run(), AND LAST IN THE WHOLE RUN. This is the one thing the
    // App Manager does that leaves the app: the platform opens a browser over
    // the Shell and stops turning its event loop. Anything sequenced behind it
    // -- the Modules tab, the sidebar, a screenshot -- would be waiting on a
    // suspended process. It settles on the app first, so what just happened is
    // on screen long enough to be recorded.
    void openLinks();

private:
    // Every row, with what this build may do with it. The interesting half is
    // the REFUSALS: a native-only package listed as unavailable with a reason
    // and no install control is a criterion in its own right, and a catalog is
    // the only place it can be observed rather than unit-tested.
    void reportCatalog();
    // Turn the event loop for `ms`, so what just happened is on screen long
    // enough to be seen.
    void settle(int ms);
    // Press the sidebar tile and wait for the page.
    bool openInstalledApp(const QString& packageName);

    BundledSetShellHost* m_host;  // not owned
    basecamp::appmanager::CatalogSource m_source;
};
