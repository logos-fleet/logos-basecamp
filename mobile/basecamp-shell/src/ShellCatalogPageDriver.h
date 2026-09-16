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
#pragma once

#include "ShellSceneDriver.h"

#include <QString>

class QQuickItem;

class BundledSetShellHost;

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
    bool run();

private:
    // Every row, judged. Split from run() so that opening the section and
    // reading it are separable failures.
    bool checkRows();
    // The scene this item is in, as a PNG under the app's data directory, and
    // the line to report it by. Empty when it could not be written.
    QString savePicture(QQuickItem* item);

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
};
