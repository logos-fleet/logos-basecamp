// OPENING A WEB APP AND LEAVING IT AGAIN (#110).
//
// ShellAppDriver presses a Bundled app's tile and asserts the app is on screen,
// and that is exactly the check that passed while the defect was live: a `web`
// app's page WAS on screen -- over the whole window, with the Shell's sidebar,
// navigation bar and close button underneath it and nothing left to press. The
// run said `web app wallet_ui is on screen` and stopped there.
//
// So this driver asserts the other half, which is the half a user needs:
//
//   1. the page is inset to the Shell's content area, not to the window --
//      there are rows of Shell outside the page's rect;
//   2. navigating away really puts the Shell back in front;
//   3. coming back brings the app back;
//   4. closing it takes the page off screen and leaves the module RUNNING --
//      closing an app is not an unload (BundledSetShellHost::unmountApp).
//
// Every one of those is false when the page owns the window, and (1) is the
// only one a driver can even attempt then.
#pragma once

#include "ShellSceneDriver.h"

#include <QStringList>

class BundledSetShellHost;

class ShellWebAppDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellWebAppDriver(BundledSetShellHost* host, QWidget* shellWidget,
                      QObject* parent = nullptr);

    // Whether this build has anything to open: a `web` module whose page is a
    // user interface. A Bundled set with no `web` member has none and says so
    // rather than failing.
    bool hasWork() const;

    void run();

private:
    // The web modules the container has opened a UI page for -- i.e. the ones
    // the sidebar carries a tile for.
    QStringList openWebApps() const;
    // Bring this build's own `web-modules` tree up, the way the Module Manager's
    // Load button does. A shipped `web` module is discovered by the core exactly
    // as an installed one is and is NOT loaded at startup, so a run that only
    // looked at what was already open would have nothing to open.
    void loadShippedWebModules();
    void settle(int ms);

    BundledSetShellHost* m_host;  // not owned
};
