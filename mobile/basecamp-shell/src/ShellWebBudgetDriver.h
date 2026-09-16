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
//   3. asks the container to answer a memory warning, and says what that shed.
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

#include <QStringList>

class BundledSetShellHost;

class ShellWebBudgetDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellWebBudgetDriver(BundledSetShellHost* host, QWidget* shellWidget,
                         QObject* parent = nullptr);

    // Whether this build carries a `web` module with a user interface. A build
    // with none has nothing to weigh and says so rather than failing.
    bool hasWork() const;

    void run();

private:
    // The `web` modules the sidebar carries a tile for -- the ones a user could
    // open, which is the set this measures.
    QStringList tiledWebApps() const;
    // Bring up the shipped `web` tree the way the Module Manager's Load button
    // does, so a measurement is not limited to whatever the Bundled set
    // happened to start.
    void loadShippedWebModules();
    // Open one app from its sidebar tile and report what the app weighs with it
    // live. False when the tile could not be pressed or the page never came up
    // -- and then the pass stops, because every line after it would be about a
    // different number of runtimes than it claims.
    bool openAndWeigh(const QString& app);
    // One line: the live count, the app's own footprint, and the budget it is
    // being read against.
    void weigh(const QString& occasion);

    BundledSetShellHost* m_host;  // not owned
};
