#pragma once

#include <QString>

#include <optional>

namespace basecamp::web {

// DRIVING A `web` APP'S PAGE: the script a host injects, and the one line of
// console vocabulary it answers in.
//
// WHY THIS EXISTS (logos-workspace#174). A `web` variant's UI is pixels in a
// canvas: there is no widget to press, no text node to read and no
// QQuickItem an in-app driver can find by objectName -- ShellSceneDriver, which
// is how every other pass presses something, sees nothing inside a page. And
// the thing that used to stand in for that from outside no longer works:
// `idb ui tap|text` is accepted and silently dropped under Xcode 27's
// CoreSimulator (measured on this venue: a tap on Safari's OWN address bar and
// `ui button HOME` do nothing either, while the companion logs `hid succeeded`),
// `simctl` has never had an input verb, and `--call` cannot reach a `ui_qml`
// module's `.rep` SLOTs. So an app that has to show that a real key reaches a
// module's form has to put the events in itself.
//
// A CONTROL HAS TWO POSSIBLE HANDLES, and this uses whichever it has.
//
//   ITS objectName, asked of the bundled QML runtime -- `logosViewItem`, which
//   is LogosWebRuntime::describeItem. It answers the item's rect in the
//   window's coordinates and the text it now HOLDS, which is the module's own
//   state and the only honest readback there is. objectName because that is
//   the handle every Logos view already carries for UI automation: the desktop
//   inspector finds items by it and nothing on the QML side has to know about
//   this.
//
//   ITS ACCESSIBLE NAME, off Qt for WebAssembly's own accessibility DOM -- a
//   mirror of the scene inside `.qt-window-a11y-container`, one element per
//   accessible item with its ARIA role and its real client rect. It is built
//   LAZILY: the tree is empty until something clicks the hidden button Qt
//   leaves in the container for a screen-reader user. This is what a BUTTON
//   has -- a Logos button carries no objectName and its text is its name.
//
// The runtime is asked first, and the reason is the whole shape of this: Qt's
// wasm bridge publishes a text editor as an `<input aria-hidden="true">` with
// NO NAME AT ALL -- measured -- so the one control a typed flow is about is
// exactly the one the accessibility tree cannot be asked for.
//
// The events themselves are REAL: the same calibrated pointer and key events
// the browser end-to-end and the smoke's web-module pass dispatch (see
// WebModuleRunner's kInputDriver, whose findings this shares -- the offset a
// Qt wasm window reads cannot be set and has to be measured per engine, pointer
// capture has to be defused, and the keys cannot go in the same turn of the
// page's event loop as the press). Only the LOCATING is asked for; a control
// is pressed where it says it is and the module is what decides what that
// means.
//
// FIRE AND FORGET, SO THE ANSWERS COME BACK ON THE CONSOLE. A platform page's
// `evaluateJavaScript` returns nothing to Qt (see PlatformPage), and the
// container already forwards a page's console to the app's log -- so every
// answer here is one `logos-drive:` line, and a driver waits for the line it
// asked for.
class WebPageInput {
public:
    // The script itself. Installs `window.logosDrive` and returns immediately
    // when it is already there, so a caller can send it before every call
    // rather than remembering which page has it.
    static QString driverScript();

    // Every reachable control, with its name and where it is. The first thing
    // to send at a page that answered nothing: it separates "this module names
    // no control" from "the accessibility tree never woke up".
    static QString describeCall();

    // Press one control -- as a mouse, and a moment later as a finger, because
    // a webview may act on one path and not the other.
    static QString pressCall(const QString& control);

    // ...THE SAME PRESS, ARMED IN THE PAGE AND FIRED `afterMs` LATER.
    //
    // WHY A HOST WOULD WANT THAT (logos-workspace#238). A page's JS thread and
    // the HOST's event loop are not the same thread: a `web` module's outbound
    // call crosses to the host, and the host answers it synchronously -- so a
    // module call that takes seconds stops the host answering anything, the
    // driver included, while the page goes on running. A flow whose second
    // press has to land DURING such a call therefore cannot wait for the first
    // press to be confirmed: by the time the host can read the confirmation,
    // the work is over. Both presses are sent before the host blocks and the
    // PAGE sequences them, which is also what a person with two fingers does.
    //
    // `afterMs` of 0 is the plain call.
    static QString pressCall(const QString& control, int afterMs);

    // Press a field and type into it, then read it back. The readback is the
    // point: it is the module's own state, not the driver's report of what it
    // dispatched.
    static QString typeCall(const QString& control, const QString& text);

    // What a control holds now, without touching it.
    static QString readCall(const QString& control);

    // The prefix every answer carries. A page's whole console crosses to the
    // app's log -- a 26 MB QML runtime's included -- and this is what a driver
    // picks its own answers out by.
    static QString marker();

    // ── reading the answers ────────────────────────────────────────────────
    //
    // Separate from the driver that waits for them so they can be stated
    // against fixed strings in a unit test, on a desktop with no webview in it.

    // "'<control>' now '<value>'" -- the value, for THAT control. Nothing when
    // the line is some other answer, or is about some other control. A password
    // field answers "<n> character(s)" rather than its contents, which comes
    // back as that text: a driver must not print a passphrase to a device
    // console.
    static std::optional<QString> valueReported(const QString& line, const QString& control);

    // "pressed '<control>'".
    static bool pressReported(const QString& line, const QString& control);

    // The page looked and there is no such control. Carries the names there
    // ARE, so a driver's failure line names them too.
    static bool refusalReported(const QString& line, const QString& control);
};

} // namespace basecamp::web
