// "A `web` APP'S OWN CONTROLS ARE PRESSED, AND THE PAGE SAYS WHAT THEY DID",
// asked from inside the app.
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
// SEVERAL NAMED FLOWS PER APP, AND A RUN PICKS ONE (logos-workspace#238). This
// driver arrived knowing exactly one flow per app -- wallet_ui's Advanced-tab
// seed import -- and the wallet then grew a second thing worth driving: the
// Private tab's accumulator sync, the `Sync now` and `Cancel` #235 shipped and
// could not check on a device. With one flow per app the only way to reach it
// was to REPLACE the first, trading one uncheckable flow for another. So the
// catalogue is WebDriveFlows and a launch says which it wants:
//
//     --drive web-input                        the app's first flow, as before
//     --drive web-input:private-sync           one named flow
//     --drive web-input:seed-import,web-input:private-sync    both, in order
//
// WHAT IT ASSERTS, and what it deliberately does not. It presses controls,
// types into fields, reads one back, and waits for lines the MODULE printed. A
// field that holds what was typed is the module's own state, arrived at through
// real pointer and key events crossing the container into the page. Whether a
// form's button then did the right thing is the module's business UNLESS the
// flow says otherwise: the seed import's press is made and whatever the module
// says about it is on the console, unasserted, because a wallet's import
// failing for want of a keystore grant is not this pass reporting a defect in
// itself. The private sync's presses ARE asserted, because there the module's
// own announcement is the only verdict there is (WebDriveFlows.h).
//
// IT LEAVES THE APP OPEN. Every other pass puts the Shell back; this one ends
// with the app on screen in whatever state the flow left it, which is the one
// state a screenshot of the run is worth taking.
#pragma once

#include "ShellSceneDriver.h"
#include "WebDriveFlows.h"

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

    // Whether this build carries a `web` app whose flows this driver knows. A
    // set without one reports that it had no work rather than failing.
    bool hasWork() const;

    // Walk the named flows, in the order given; an empty list walks the one
    // flow a plain `--drive web-input` has always walked. Runs after the other
    // scene passes for the reason every page-opening pass does: the window goes
    // to a platform view and the Shell's own chrome is no longer what is on
    // screen.
    void run(const QStringList& flowNames = {});

private:
    // The `web` app this build would drive for `flowName`: the first tiled one
    // that carries it. Empty when there is none.
    QString appFor(const QString& flowName) const;

    // One flow, start to finish. False having said why.
    bool walk(const basecamp::shell::WebDriveFlow& flow);

    // Press the app's sidebar tile and wait until the Shell has docked a page
    // for it. Returns false having said why. A page already on screen is
    // ALREADY OPEN and is not pressed again -- a second flow on the same app
    // must not toggle the first one's window away.
    bool openApp(const QString& app);

    // Send one script into the page and wait for the answer the caller is
    // after. `answered` is asked of every page line from `from` on; false when
    // none satisfied it inside the budget. `from` is advanced past the line
    // that answered, so the step after this one reads what it did not.
    bool ask(const QString& app, const QString& call,
             const std::function<bool(const QString&)>& answered, int budgetMs, int& from);

    // Wait for a page line WITHOUT sending anything: the verdict of a step
    // whose answer is the module's own announcement rather than a control's
    // contents. Reads from `from`, which is where the step BEFORE it started --
    // a module may answer a press before the page has finished reporting it.
    bool watch(basecamp::shell::WebPageWatcher& watcher, int budgetMs, int& from);

    BundledSetShellHost* m_host;  // not owned
    // Every page line since this driver started listening. A member because the
    // container delivers them on its own signal, not in reply to anything.
    QStringList m_pageLines;
};
