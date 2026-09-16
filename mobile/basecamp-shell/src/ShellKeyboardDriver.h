// "Tapping a field in a mounted app brings the keyboard up", asked from inside
// the app.
//
// There is no other way to ask it here. Xcode 27 removed SimulatorKit, so
// `idb ui tap` refuses HID on a modern simulator, and `simctl` has no input
// verb at all -- so the tap and the answer both have to come from in-process.
// That is fine, because the thing being measured is not UIKit's delivery of
// the touch: it is what Qt does with the focus afterwards, and that is
// entirely visible from here.
//
// The three facts it reports are the three the platform uses, in order:
//
//   1. does the QML item have activeFocus?  (the caret)
//   2. is QGuiApplication::focusObject() one that answers Qt::ImEnabled?
//      (QIOSInputContext::update() raises the keyboard when, and only when,
//      this is true -- QInputMethod::show() is a no-op on iOS)
//   3. is QInputMethod::isVisible(), and how tall is the keyboard rectangle?
//
// Those three also settle the question logos-workspace#152 opens with. A run
// where (1) and (2) hold and (3) does not is a HARDWARE KEYBOARD attached to
// the simulator -- the field is focused, the input method is on it, and the
// platform is deliberately not drawing a panel. A run where (2) is false is the
// defect: nothing the user can do will raise a keyboard.
//
// The path is the operator's: the app's "+" menu, New DM, and the address field
// in the dialog it opens. Nothing here names chat beyond that path, and a set
// that carries no app it knows one for reports that it had no work.
#pragma once

#include "ShellSceneDriver.h"

#include <QImage>

class BundledSetShellHost;
class QQuickWidget;

class ShellKeyboardDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellKeyboardDriver(BundledSetShellHost* host, QWidget* shellWidget,
                        QObject* parent = nullptr);

    // Whether the mounted app is one whose field path this driver knows.
    bool hasWork() const;

    // Opens the app's new-conversation dialog, taps its field and reports the
    // three facts. Leaves the dialog closed again. Runs after ShellAppDriver:
    // the app has to be on screen before its "+" exists.
    void run();

private:
    // The handles of the path from a mounted app to one of its input fields.
    struct FieldPath {
        QString menuButton;   // the control that opens the app's "new" menu
        QString menuItem;     // the entry in it that opens a dialog with a field
        QString field;        // the field in that dialog
    };
    static FieldPath fieldPathFor(const QString& appName);

    // Everything between the opened "+" menu and the verdict: the entry, the
    // dialog, the tap on its field and the three facts. Split out so that
    // run() has one place to close what the menu opened, whether this reached
    // the field or gave up short of it.
    //
    // `beforeTheMenu` is the app surface's frame from before anything opened,
    // so what the menu draws can be told from what was already there.
    void askTheField(const FieldPath& path, QQuickWidget* surface,
                     const QImage& beforeTheMenu);

    // Whether `item` left a mark on the app's surface when it appeared -- the
    // one question in this pass that is about what the USER sees rather than
    // about a property (logos-workspace#187). Prints the ancestry when it did
    // not, because the level where the size or the visibility was lost is the
    // level with the bug.
    void reportPainted(const QString& name, QQuickItem* item, QQuickWidget* surface,
                       const QImage& before);

    // The view module whose app is up, or an empty string. Both hasWork() and
    // run() are about that one app.
    QString appOnScreen() const;

    // Close whatever the walk left open. Escape at the SURFACE is what a
    // Popup's closePolicy CloseOnEscape listens for -- the dialog's Cancel
    // button carries no handle -- so `anchor` only has to be an item in the
    // same scene as the popups, not the popup itself.
    void dismiss(QQuickItem* anchor);

    BundledSetShellHost* m_host;  // not owned
};
