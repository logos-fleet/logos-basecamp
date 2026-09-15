// "TYPED TEXT REACHES A `web` APP'S FORM", asked from inside the app.
//
// The pass logos-workspace#174 exists for. A wallet flow whose input is typed
// -- the seed-phrase import, a Send's recipient and amount, the Advanced tab's
// custom RPC -- could not be driven on a device at all: `idb ui text` is
// accepted and dropped under Xcode 27's CoreSimulator (so is `idb ui tap`, and
// so is `ui button HOME` -- measured against Safari's OWN chrome on this
// venue's iPad simulator, with the companion logging `hid succeeded` for every
// one), `simctl` has no input verb, and `--call` answers `(no value)` for a
// `ui_qml` module's `.rep` SLOTs. Nothing outside the app could put a key in a
// page.
//
// So the app does it, the way #152 answered the same wall for the Shell's own
// QML: ShellKeyboardDriver walks the operator's path inside the process and
// reports what the platform did with it. This is that shape for a page, and the
// piece that made it possible is that Qt for WebAssembly publishes the scene as
// an accessibility DOM -- names and rects for every control, and the field's
// CONTENTS afterwards. WebPageInput.h holds the account of that.
//
// WHAT IT ASSERTS, and what it deliberately does not. It presses a form open,
// types into its fields and reads one back. A field that holds what was typed
// is the module's own state, arrived at through real pointer and key events
// crossing the container into the page -- which is the fact the issue asks for.
// Whether the form's BUTTON then did the right thing is the module's business:
// the press is made and whatever the module says about it is on the console,
// unasserted, because a wallet's import failing for want of a keystore grant is
// not this pass reporting a defect in itself.
//
// IT LEAVES THE APP OPEN. Every other pass puts the Shell back; this one ends
// with the form on screen holding what it was given, which is the one state a
// screenshot of the run is worth taking.
#pragma once

#include "ShellSceneDriver.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class BundledSetShellHost;

class ShellWebInputDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellWebInputDriver(BundledSetShellHost* host, QWidget* shellWidget,
                        QObject* parent = nullptr);

    // Whether this build carries a `web` app whose typed path this driver
    // knows. A set without one reports that it had no work rather than failing.
    bool hasWork() const;

    // Open the app from its sidebar tile, walk its form and report. Runs after
    // the other scene passes for the reason every page-opening pass does: the
    // window goes to a platform view and the Shell's own chrome is no longer
    // what is on screen.
    void run();

private:
    // ONE STEP OF A TYPED FLOW, named the way the page names its controls --
    // an accessible name, which for a Logos text field is its placeholder. Text
    // empty means "press this", text set means "type this into it".
    struct Step {
        QString control;
        QString text;
    };
    // The flow this driver knows for an app, and which of its fields is the one
    // read back for the verdict. Empty when the app is not one it knows.
    struct TypedFlow {
        QList<Step> steps;
        QString     verdictField;   // must end up holding verdictText
        QString     verdictText;
    };
    static TypedFlow flowFor(const QString& app);

    // The `web` app this build would drive: the first tiled one whose flow is
    // known. Empty when there is none.
    QString appToDrive() const;

    // Press the app's sidebar tile and wait until the Shell has docked a page
    // for it. Returns false having said why.
    bool openApp(const QString& app);

    // Send one script into the page and wait for the answer the caller is
    // after. `answered` is asked of every `logos-drive:` line the page prints
    // from here on; false when none satisfied it inside the budget.
    bool ask(const QString& app, const QString& call,
             const std::function<bool(const QString&)>& answered, int budgetMs);

    BundledSetShellHost* m_host;  // not owned
    // Every page line since this driver started listening. A member because the
    // container delivers them on its own signal, not in reply to anything.
    QStringList m_pageLines;
};
