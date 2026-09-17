// WHAT THE LIVE-RUNTIME BUDGET COSTS ON THIS DEVICE (logos-workspace#153).
//
// The budget used to be a fixed count of ONE on every device, defended by a
// constant sampled once on a Samsung, and the app's own memory -- which the
// container already measures -- decided nothing. #153 asks for the count to
// come from the device and for the measurement to be read; it also asks, before
// either, for the NUMBERS: what the app weighs with one, two and three `web`
// runtimes live, on each of the venue's three memory profiles.
//
// This pass is how those numbers are taken, and it is deliberately a pass
// rather than a one-off script: a policy derived from a measurement nobody can
// repeat is the same mistake as the constant this issue is about.
//
// WHAT IT DOES, in order:
//
//   1. loads every `web` module the image ships that is not already up;
//   2. opens them one at a time from the sidebar, the way a user would, and
//      after each one says what the app weighs and how many UI runtimes are
//      live -- so the line for 1, 2 and 3 live runtimes is the same line three
//      times and the difference between them is the cost of a page;
//   3. asks the container to answer a memory warning, and says what that shed;
//   4. ASSERTS that the figure the budget reads actually moved with the pages
//      (logos-workspace#244).
//
// STEP 4 IS THE ONE THAT FAILS ON A BLIND METRIC. #153's ceiling was weighed
// against this process's own resident size, which on Android is blind to the
// pages: measured 2026-09-17, the app's figure read 435/434/435 MB for one, two
// and three live runtimes, and on the warning that shed two pages it went UP
// 3 MB while the renderer holding them fell 65 MB. A ceiling read off that can
// never trip, and would drive the opposite decision if it did. So the pass
// records the weighed figure at 0, 1, ... N live runtimes and again after the
// shed, and checks three things:
//
//   * all the pages together moved it up by at least a third of a renderer;
//   * the FIRST page did, on its own -- a renderer starting is ~290 MB;
//   * and SHEDDING TOOK IT BACK DOWN, which is the discriminating one. The
//     blind reading passes the first two (the app does allocate to start a
//     renderer; it just cannot see what the renderer then holds) and fails only
//     on the sign.
//
// The steps past the first are PRINTED AND NOT ASSERTED, because #230 weighed
// every page of a build into one renderer: pages two and three cost ~5 MB each,
// under the noise of a figure the whole device is in. On the venue's 14.9 GB
// Lenovo the second page's step read -62 MB with nothing shed.
//
// Step 3 is the synthetic half and says so: it drives the container's own
// handler, which is the same code path the OS's notification lands on
// (AppMemory's watchAppMemoryPressure). The REAL signal is checkable on Android
// with `adb shell am send-trim-memory <pkg> RUNNING_CRITICAL`, and that is a
// different claim -- that the platform half is wired -- which this pass cannot
// make for itself.
//
// IT NEEDS A BUDGET BIGGER THAN ONE to measure anything: with the device's own
// number on a small phone there is never a second live runtime. A run that
// wants the three-runtime figure says so with `LOGOS_WEB_RUNTIME_BUDGET=3`, and
// this pass reports the budget it ran under so a log cannot be read as the
// device's answer when it was the environment's.
#pragma once

#include "ShellSceneDriver.h"

#include <QList>
#include <QPair>
#include <QStringList>

class BundledSetShellHost;

class ShellWebBudgetDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellWebBudgetDriver(BundledSetShellHost* host, QWidget* shellWidget,
                         QObject* parent = nullptr);

    // Whether there is anything here to weigh: a `web` app the sidebar already
    // carries, or a shipped module that might become one once it is loaded. A
    // build with neither says so rather than failing.
    bool hasWork() const;

    void run();

private:
    // The `web` modules the sidebar carries a tile for.
    QStringList tiledWebApps() const;
    // ...and the ones of those the container actually has a page for, which is
    // the set this measures. A tile whose module refused to load is named and
    // left out rather than pressed: see the definition for what pressing it
    // cost (#230).
    QStringList openableWebApps();
    // The shipped modules outside the Bundled set that have no page yet -- what
    // loadShippedWebModules() is about to ask for, and the answer to whether
    // there is anything to wait on.
    QStringList shippedWebModulesToLoad() const;
    // Bring up the shipped `web` tree the way the Module Manager's Load button
    // does, so a measurement is not limited to whatever the Bundled set
    // happened to start.
    void loadShippedWebModules();
    // Open one app from its sidebar tile and report what the app weighs with it
    // live. False when the tile could not be pressed or the page never came up
    // -- and then the pass stops, because every line after it would be about a
    // different number of runtimes than it claims.
    bool openAndWeigh(const QString& app);
    // One line: the live count, the app's own footprint, what the budget
    // actually weighs on this platform, and the budget it is read against.
    // Records the weighed figure against the live count for step 4.
    void weigh(const QString& occasion);
    // Step 4's verdict, from what weigh() recorded plus the figure taken after
    // the memory warning settled. Prints one WRONG line per broken claim and
    // one OK line when the figure tracked the pages.
    void reportWhetherTheFigureMoved(qint64 afterShedBytes, int pagesShed);

    BundledSetShellHost* m_host;  // not owned
    // The weighed figure at each live-runtime count the pass passed through,
    // most recent last, paired with the count it was taken at.
    QList<QPair<int, qint64>> m_weighed;
};
