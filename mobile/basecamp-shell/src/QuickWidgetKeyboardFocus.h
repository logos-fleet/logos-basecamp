// THE ON-SCREEN KEYBOARD, FOR QML THAT LIVES INSIDE A WIDGET.
//
// A view module's QML is mounted in a QQuickWidget, which renders into an
// OFFSCREEN QQuickWindow. That gives the scene two focus notions, and only one
// of them reaches the platform:
//
//   the scene's   the QQuickWidget's offscreen window has its own focus
//                 object -- the TextArea the user tapped, with activeFocus,
//                 a caret and a blinking cursor.
//   the window's  the platform input context is told about
//                 QGuiApplication::focusObject(), which is the top-level
//                 QWidgetWindow's focus WIDGET (QWidgetWindow::focusObject()).
//
// QQuickWidget bridges the two: it answers Qt::ImEnabled and every other
// input-method query out of its scene's focus object, so once the WIDGET is the
// window's focus widget the platform asks the right item. But it only
// propagates a scene focus change upward when it is already the application's
// focus object (QQuickWidget::propagateFocusObjectChanged), and nothing on iOS
// ever makes it one:
//
//   * QIOSIntegration reports SetFocusOnTouchRelease = true, so
//     QApplicationPrivate::giveFocusAccordingToFocusPolicy returns without
//     setting focus on TouchBegin and waits for the release;
//   * QApplication::notify only calls that function for TouchBEGIN -- there is
//     no TouchEnd case -- so the release never comes;
//   * and the synthesised mouse press that WOULD have carried the click focus
//     never happens, because QQuickWidget sets Qt::WA_AcceptTouchEvents and
//     accepts the touch itself.
//
// So on an iPad the field takes the caret and QGuiApplication::focusObject()
// stays on whatever widget had it -- typically the shell window, which is not
// input-method enabled. QIOSInputContext::update() then reads
// inputMethodAccepted() == false and resigns its text responder instead of
// making it first responder, and no keyboard appears. QInputMethod::show(),
// which QQuickTextInput calls on focus-in, is documented as a no-op on iOS:
// "keyboard controlled fully by platform based on focus".
//
// This restores the missing half. It watches every QQuickWidget in the process
// and, whenever a scene's focus object is one that wants a keyboard, gives that
// widget the window's focus -- which is what a mouse press does on the desktop
// and what the touch path skips here. It is deliberately driven by the SCENE's
// focus change rather than by the tap: chat_ui's New DM dialog focuses its
// address field from `onOpened`, with no tap on the field at all, and a
// tap-only fix would leave that one silent.
//
// logos-workspace#152.
#pragma once

#include <QObject>
#include <QSet>

class QQuickWidget;

class QuickWidgetKeyboardFocus : public QObject
{
    Q_OBJECT
public:
    explicit QuickWidgetKeyboardFocus(QObject* parent = nullptr);

    // Watch every QQuickWidget in this process, including ones created later.
    // An app is mounted into a widget the host makes on demand, so a list taken
    // at startup would miss exactly the scene this is for.
    void watchEverything();

    // Keep the invariant for one surface from now on, and once immediately: a
    // scene that already holds a focused text item has already asked for a
    // keyboard. Idempotent.
    void watch(QQuickWidget* surface);

    // Whether `object` is one the platform would raise a keyboard for. The same
    // question QGuiApplication asks of the focus object
    // (QInputMethodPrivate::objectAcceptsInputMethod), asked the same way.
    static bool acceptsInputMethod(QObject* object);

    // Make `surface` its window's focus widget, so the platform input context
    // is pointed at the scene inside it. Returns whether it now holds it.
    static bool takeKeyboardFocus(QQuickWidget* surface);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void reconcile(QQuickWidget* surface);

    QSet<QObject*> m_watched;
};
