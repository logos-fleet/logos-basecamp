// THE CATALOG, READ OFF THE SCREEN.
//
// ShellCatalogDriver next door DOES things with a catalog -- it adds a
// repository, installs a row and checks the module reached the workspace -- and
// it reports every row's verdict from `StoreAppManager::catalogEntries`, the
// MODEL. This one asserts the other half, which is the half a user has:
// pressing the Applications button in the sidebar and reading the page.
//
// WHY THAT IS A SEPARATE CLAIM (logos-workspace#169, acceptance 3). A Store
// shell's Platform floor is derived by the build and applied to every catalog
// row: a Downloaded module that reaches a Platform module this build did not
// bundle is listed unavailable, because installing it would succeed and the
// module would die at its first call (ADR 0007's honest-availability rule, ADR
// 0009's second). All of that was true and provable before this driver -- in a
// unit test, and in a console line. Neither is a screen. A user cannot read a
// build log, and "the verdict is in a model nothing is bound to" is exactly the
// state a rendered page is the remedy for.
//
// WHAT IT ASSERTS, per row, against the App Manager's own model:
//
//   1. the page draws a row for every entry the catalog published -- SCROLLED
//      end to end, because a ListView instantiates only the delegates around
//      its viewport and a claim about one frame would be a claim about the
//      first dozen rows
//   2. a row that may be installed HAS an install control, and that control is
//      where a finger could reach it -- checked without pressing it, because
//      pressing it starts a download
//   3. a row that may NOT has NO install control at all, rather than a disabled
//      one, and carries the reason it was refused in -- the same sentence the
//      model holds, so the page cannot soften or lose it
//
// It presses the sidebar rather than setting the section index, for the reason
// ShellPackageSectionDriver does: the defect this kind of page has is that a
// user gets there and finds nothing, and the only way to see that is to go
// there the way they do.
//
// ...AND, WITH `--drive catalog:install`, IT PRESSES INSTALL (#249).
//
// Reading the page is a claim about a layout; the assertions above deliberately
// do NOT press the control, because pressing it starts a download. That left
// exactly one thing about this page unchecked, and it was the thing that broke:
// every automated proof that installing works drives `--install`, which is
// ShellCatalogDriver -> ShellModulesBackend::installFromCatalog -- a route that
// approves the signer IN C++ and never touches the page. So the user-facing
// control was pressed by nobody, and when the signer prompt it publishes turned
// out to have no view bound to it, the whole install flow was dead for days
// with every check green. An operator found it by hand.
//
// The install flow asserts, in the order a finger meets it:
//
//   1. the row the model calls installable has a control a press reaches, and
//      pressing it puts the SIGNER GATE on screen -- the step #249 was missing
//   2. the gate names the signer AND the DID, read off the scene rather than
//      out of the model
//   3. approving it installs: the package becomes a Downloaded module the core
//      knows about
//   4. and the page then tells the truth about it -- the gate is gone, the row
//      offers no second install, and it says which version is on the device
//
// A REFUSAL IS NOT A FAILURE of this pass, and neither is a catalog with
// nothing installable (every row already installed is what a SECOND launch
// looks like). What is a failure is a press that produces neither a gate nor a
// sentence -- which is #249 exactly, and is what this pass exists to catch.
#pragma once

#include "ShellSceneDriver.h"

#include <QString>
#include <QStringList>

class QQuickItem;

class BundledSetShellHost;

namespace basecamp::appmanager { class StoreAppManager; }

class ShellCatalogPageDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellCatalogPageDriver(BundledSetShellHost* host, QWidget* shellWidget,
                           QObject* parent = nullptr);

    // Open the Applications section and check the page against the model.
    // Returns false on a page that contradicts the model, or one that could not
    // be opened at all. A build with no catalog is NOT a failure -- a Store
    // shell's Bundled set is data (ADR 0007), and a set without the package
    // modules is a shell whose page has one honest sentence on it.
    //
    // `options` are `--drive catalog:<option>`, in the order the run asked:
    // `install` presses the first installable row, `install=<package>` that
    // one. Empty is the historic behaviour -- read the page, press nothing.
    bool run(const QStringList& options);

private:
    // Every row, judged. Split from run() so that opening the section and
    // reading it are separable failures.
    bool checkRows();
    // One `--drive catalog:` option, driven. False only when the page or the
    // run is wrong -- see the header comment on what is not a failure.
    bool driveOption(const QString& option);
    // Press Install on this row, walk the signer gate, and check the package
    // arrived. `packageName` empty means "the first row the model says may be
    // installed here", which is what a bare `catalog:install` asks for.
    bool driveInstall(const QString& packageName);
    // The three steps of that walk, each one a separable failure -- the same
    // reason checkRows() is not part of run().
    //
    // What a press that raised NO gate has to have left behind instead: a
    // refusal, on the page, in the words the App Manager refused it in. True
    // when it did, which is a legitimate end to this pass.
    bool checkRefusedWithoutGate(basecamp::appmanager::StoreAppManager* manager,
                                 const QString& wanted);
    // What the gate that DID come up has to say, read off the scene rather
    // than out of the model -- and photographed while it is up.
    bool checkGateOnScreen(QQuickItem* title);
    // ...and what the page has to say once the install is done: no gate over
    // it, no second install control, and a line naming what is on the device.
    bool checkRowAfterInstall(const QString& wanted, QQuickItem* list);
    // Scroll `list` until an item with this objectName exists with its centre
    // inside the viewport, and return it. A ListView instantiates only the
    // delegates around its viewport, so a row further down the catalog has no
    // control to press until the list has been taken to it.
    QQuickItem* scrollTo(const QString& objectName, QQuickItem* list);
    // Spin until nothing in the scene answers to this objectName. Used for the
    // gate coming back down, which is the page's own report that the install
    // finished rather than stalled behind a modal.
    bool waitUntilGone(const QString& objectName, int timeoutMs);
    // The scene this item is in, as a PNG under the app's data directory named
    // `<basename>.png`, and the line to report it by. Empty when it could not
    // be written. The basename is the caller's because a run takes more than
    // one -- a refused row, and the signer gate -- and one fixed filename meant
    // the second write destroyed the first.
    QString savePicture(QQuickItem* item, const QString& basename);

    BundledSetShellHost* m_host;  // not owned
    // Where the picture of the first refused row went, once one has been taken.
    QString m_picture;

    // How long a row's delegate may take to exist after the page appears. A
    // view that has just become visible instantiates its delegates over the
    // next few ticks, and the catalog's rows arrive from a network fetch that
    // may still be in flight when the section opens.
    static constexpr int kRowTimeoutMs = 8000;
    // And how long a scroll step is given before the rows it brought into the
    // viewport are looked for. Delegate creation is asynchronous, and a step
    // read too early reports a page that is merely slower than this driver.
    static constexpr int kScrollSettleMs = 400;
    // How long the signer gate may take to appear after Install is pressed.
    // Generous, because the press runs the whole download inside the event it
    // was delivered by: package_downloader fetches the index and then the
    // package over the network before the gate has anything to show.
    static constexpr int kGateTimeoutMs = 60000;
    // ...and how long the install behind an approval may take. Shorter: the
    // bytes are already on disk, and what happens here is package_manager
    // unpacking them plus the core's rescan and load.
    static constexpr int kInstallTimeoutMs = 30000;
    // ...and how long the gate has to come back down once it has. Shorter
    // again: nothing is fetched or unpacked here, the model has already
    // changed, and what is waited for is the frame that shows it.
    static constexpr int kGateDownTimeoutMs = 5000;
    // How much of the event loop one turn of a wait above spends. Small enough
    // that the deadline is met closely, large enough that the work being
    // waited on actually runs in it.
    static constexpr int kPollSliceMs = 100;
};
