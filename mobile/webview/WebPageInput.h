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
// THE HANDLE IT USES IS THE ACCESSIBILITY TREE, and that is the finding this
// file is built on. Qt for WebAssembly maintains a DOM mirror of the scene --
// one element per accessible item, with its ARIA role, its name and its real
// client rect -- inside `.qt-window-a11y-container`, and it builds it lazily:
// the tree is EMPTY until something clicks the hidden button Qt leaves in the
// container for a screen-reader user. Woken up, it gives a driver the three
// things a canvas denies it:
//
//   * WHERE a control is, in client coordinates, without a screenshot and
//     without coordinates baked into a recipe per device size;
//   * WHAT it is called -- `aria-label`, which for a Logos text field is its
//     placeholder (logos-design-system, LogosTextField);
//   * WHAT IT NOW HOLDS, read back off the element after the keys went in,
//     which is the difference between "the driver dispatched events" and "the
//     module got the text".
//
// The events themselves are REAL: the same calibrated pointer and key events
// the browser end-to-end and the smoke's web-module pass dispatch (see
// WebModuleRunner's kInputDriver, whose findings this shares -- the offset a
// Qt wasm window reads cannot be set and has to be measured per engine, pointer
// capture has to be defused, and the keys cannot go in the same turn of the
// page's event loop as the press). Only the LOCATING is done through the
// accessibility tree.
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
