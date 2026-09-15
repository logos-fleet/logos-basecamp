#include "ShellKeyboardDriver.h"

#include "BundledSetShellHost.h"
#include "KeyboardPanel.h"
#include "QuickWidgetKeyboardFocus.h"
#include "ShellSections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethod>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QRectF>
#include <QScreen>
#include <QUrl>

namespace {

// How long a dialog gets to instantiate. It is QML in the module's own qrc and
// it is created on first open, so this is compilation plus a layout pass.
constexpr int kDialogBudgetMs = 10000;
// A menu opens with a transition; its entries exist before they are pressable.
constexpr int kMenuSettleMs = 400;
// The keyboard is animated up by UIKit, and isVisible() only becomes true once
// it has been reported. Measured on the iPad Air simulator at well under this.
constexpr int kKeyboardBudgetMs = 3000;
// And how long the panel is left standing once it has been measured. The other
// half of logos-workspace#170's answer is a picture of it, and the only way to
// take one -- `xcrun devicectl device capture screenshot` on a device, `simctl
// io screenshot` on a simulator -- is a command on the HOST that has to be
// started after the log line says there is something to photograph. A pass
// that dismissed the dialog the moment it had the numbers left nothing to aim
// at. Four seconds is that round trip with room to spare, against a run that
// spends minutes on the chat bring-up alone.
constexpr int kPanelHoldMs = 4000;

QString describe(QObject* object)
{
    if (!object)
        return QStringLiteral("<none>");
    const QString name = object->objectName();
    QString text = QStringLiteral("%1(%2)")
                       .arg(QString::fromUtf8(object->metaObject()->className()),
                            name.isEmpty() ? QStringLiteral("-") : name);
    // A QQuickWidget is never the interesting half of the answer -- it forwards
    // every input-method query to the item its own scene has focused, and WHICH
    // widget it is is the whole question when several are on screen. So the
    // scene behind it is named too.
    auto* surface = qobject_cast<QQuickWidget*>(object);
    if (!surface)
        return text;
    QQuickWindow* scene = surface->quickWindow();
    // focusObject() answers the window ITSELF when no item in it holds focus.
    QObject* inScene = scene ? scene->focusObject() : nullptr;
    const bool sceneHoldsAnItem = inScene && inScene != scene;
    const QString qml = surface->source().fileName();
    text += QStringLiteral(" over scene %1, focusing %2")
                .arg(qml.isEmpty() ? QStringLiteral("<none>") : qml,
                     sceneHoldsAnItem ? describe(inScene) : QStringLiteral("nothing"));
    return text;
}

} // namespace

ShellKeyboardDriver::ShellKeyboardDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                         QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
}

ShellKeyboardDriver::FieldPath ShellKeyboardDriver::fieldPathFor(const QString& appName)
{
    // chat_ui's "+" menu, its New DM entry and the address field in the dialog
    // that opens -- logos-chat-ui src/qml/ChatUi/ConversationsPane.qml and
    // NewConversationDialog.qml. It is the shortest path from a freshly mounted
    // app to a field, and it needs no conversation to exist first.
    if (appName == QLatin1String("chat_ui")) {
        return { QStringLiteral("newMenuButton"), QStringLiteral("newDmMenuItem"),
                 QStringLiteral("convAddressField") };
    }
    return { };
}

QString ShellKeyboardDriver::appOnScreen() const
{
    // The set's first view module: the one ShellAppDriver mounts and leaves up,
    // and an empty string when the set carries none.
    return m_host->backend()->viewModuleNames().value(0);
}

bool ShellKeyboardDriver::hasWork() const
{
    return !fieldPathFor(appOnScreen()).field.isEmpty();
}

void ShellKeyboardDriver::run()
{
    const QString app = appOnScreen();
    if (app.isEmpty()) {
        emit log(QStringLiteral("keyboard: no app in this Bundled set"));
        return;
    }
    const FieldPath path = fieldPathFor(app);
    if (path.field.isEmpty()) {
        emit log(QStringLiteral("keyboard: no input-field path is known for app '%1'").arg(app));
        return;
    }
    if (!m_host->mountedView(app)) {
        emit log(QStringLiteral("keyboard: '%1' is not mounted, nothing to type into")
                     .arg(app));
        return;
    }
    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    // ── 1. the app's own way to a field ──
    QQuickItem* menuButton = waitFor(path.menuButton, 5000);
    if (!menuButton) {
        dumpNames(QStringLiteral("app '%1' shows no '%2'").arg(app, path.menuButton));
        return;
    }
    // THE FRAME BEFORE ANY OF IT OPENS. What follows is a menu and then a
    // dialog, and #187 is that neither of them is drawn -- so each is compared
    // against this rather than against what the pass claims about it.
    QQuickWidget* surface = surfaceOf(menuButton);
    const QImage beforeTheMenu = frameOf(surface);
    if (!tap(menuButton)) return;
    settle(kMenuSettleMs);

    // From here the app has something of its own over the workspace -- the
    // menu, then the dialog -- and EVERY way out of the walk below closes it
    // again, not just the one that reached the field. The passes sequenced
    // behind this one press the Shell's own chrome, and a popup left open over
    // the workspace would swallow their taps. The "+" is the anchor because it
    // is in the same scene as the popups and is the one handle that exists on
    // all of those paths.
    askTheField(path, surface, beforeTheMenu);
    dismiss(menuButton);
}

void ShellKeyboardDriver::askTheField(const FieldPath& path, QQuickWidget* surface,
                                      const QImage& beforeTheMenu)
{
    // The items from here on are inside Popups, which hang off the scene's
    // overlay rather than off the view's root object.
    QQuickItem* menuItem = waitFor(path.menuItem, 5000, Scope::WithOverlays);
    if (!menuItem) {
        dumpNames(QStringLiteral("the '%1' menu has no '%2'").arg(path.menuButton, path.menuItem));
        return;
    }
    reportPainted(path.menuItem, menuItem, surface, beforeTheMenu);

    // AND THE FRAME WITH THE MENU ON IT, which is what the dialog is compared
    // against: pressing the entry closes the menu and opens the dialog, so a
    // diff taken from before the menu would count the menu's own disappearance
    // as the dialog being drawn.
    const QImage beforeTheDialog = frameOf(surface);
    if (!tap(menuItem)) return;

    QQuickItem* field = waitFor(path.field, kDialogBudgetMs, Scope::WithOverlays);
    if (!field) {
        dumpNames(QStringLiteral("no '%1' after '%2'").arg(path.field, path.menuItem));
        return;
    }
    emit log(QStringLiteral("keyboard: '%1' opened the dialog holding '%2'")
                 .arg(path.menuItem, path.field));
    reportPainted(path.field, field, surface, beforeTheDialog);

    // ── 2. the tap a user makes ──
    // The dialog focuses the field itself on open; tapping it is still the
    // reported action, and it is the one that has to work a second time.
    if (!tap(field)) return;

    // ── 3. the three facts, in the order the platform reads them ──
    // BOTH, and the rectangle is not implied by the flag: isVisible() turns
    // true on the platform's will-show notification and the geometry arrives
    // with it, so reading the rectangle in the same turn reports 0x0 for a
    // panel that is on its way up.
    QInputMethod* im = QGuiApplication::inputMethod();
    QElapsedTimer waiting;
    waiting.start();
    while ((!im->isVisible() || im->keyboardRectangle().isEmpty())
           && waiting.elapsed() < kKeyboardBudgetMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    QObject* focusObject = QGuiApplication::focusObject();
    const bool caret = field->hasActiveFocus();
    const bool accepted = QuickWidgetKeyboardFocus::acceptsInputMethod(focusObject);
    const QRectF keyboard = im->keyboardRectangle();

    emit log(QStringLiteral("keyboard: '%1' activeFocus=%2").arg(path.field)
                 .arg(caret ? QStringLiteral("true") : QStringLiteral("false")));
    emit log(QStringLiteral("keyboard: app focus object %1, accepts input method: %2")
                 .arg(describe(focusObject),
                      accepted ? QStringLiteral("yes") : QStringLiteral("no")));
    // The DISPLAY, not field->window(): a QQuickWidget's items live in an
    // offscreen window whose geometry is the widget's, and the panel is
    // measured against the phone.
    const QScreen* display = QGuiApplication::primaryScreen();
    const QRectF screen = display ? QRectF(display->geometry()) : QRectF();
    emit log(QStringLiteral("keyboard: QInputMethod isVisible=%1, panel %2x%3 of a %4x%5 "
                            "screen after %6 ms")
                 .arg(im->isVisible() ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(keyboard.width(), 0, 'f', 0).arg(keyboard.height(), 0, 'f', 0)
                 .arg(screen.width(), 0, 'f', 0).arg(screen.height(), 0, 'f', 0)
                 .arg(waiting.elapsed()));
    // WHICH panel, named rather than left as a number, because "isVisible=true
    // and nothing to type on" is the confusing case and it is the one every
    // simulator in the fleet reports (logos-workspace#152, #170).
    using basecamp::shell::KeyboardPanel;
    const KeyboardPanel panel = basecamp::shell::panelDrawn(im->isVisible(), keyboard, screen);
    switch (panel) {
    case KeyboardPanel::ShortcutBar:
        emit log(QStringLiteral("keyboard: that is a shortcut bar, not a keyboard -- this "
                                "device has a hardware keyboard connected, so iOS draws no "
                                "panel. The input method is still ON the field"));
        break;
    case KeyboardPanel::Keyboard:
        // Measured, so the screen has a height to divide by: anything else is
        // Unmeasured.
        emit log(QStringLiteral("keyboard: that is a full software keyboard -- %1 of %2 "
                                "points, %3% of the screen")
                     .arg(keyboard.height(), 0, 'f', 0).arg(screen.height(), 0, 'f', 0)
                     .arg(100.0 * keyboard.height() / screen.height(), 0, 'f', 0));
        break;
    case KeyboardPanel::Unmeasured:
        emit log(QStringLiteral("keyboard: the platform says a panel is up and gave no "
                                "geometry for it in %1 ms -- there is nothing here to tell "
                                "a keyboard from a shortcut bar")
                     .arg(kKeyboardBudgetMs));
        break;
    case KeyboardPanel::None:
        // Nothing was drawn, and the verdict below is the whole of what there
        // is to say about that.
        break;
    }

    if (!caret) {
        emit log(QStringLiteral("WRONG: '%1' took no focus at all from the tap -- the "
                                "dock's focus chain, not the input method")
                     .arg(path.field));
    } else if (!accepted) {
        emit log(QStringLiteral("WRONG: the caret is in '%1' and the object the input "
                                "context is handed is %2, which wants no keyboard -- "
                                "nothing the user does can raise one")
                     .arg(path.field, describe(focusObject)));
    } else if (panel == KeyboardPanel::None) {
        // Everything Qt owns is right and the platform is still not drawing a
        // panel. On a simulator that is the hardware keyboard being connected,
        // which is the first thing logos-workspace#152 asks to rule out.
        emit log(QStringLiteral("keyboard: the input method is ON the field and the "
                                "platform drew no panel of any kind -- a connected "
                                "hardware keyboard does exactly this; on a device it "
                                "would be a platform fault"));
        emit log(QStringLiteral("KEYBOARD REACHES THE FIELD (no panel drawn)"));
    } else {
        emit log(QStringLiteral("KEYBOARD REACHES THE FIELD"));
    }

    // And leave it up long enough to be photographed. AFTER the verdict, so
    // that the line a capture is timed from is the last one printed and the
    // picture is of a panel that has already been measured.
    if (panel != KeyboardPanel::None) {
        emit log(QStringLiteral("keyboard: the panel is on screen; holding it for %1 ms "
                                "so a screenshot can see it").arg(kPanelHoldMs));
        settle(kPanelHoldMs);
    }
}

// WHAT THE USER WOULD SEE, in the one place this pass can answer it. Every
// other line it prints is a property -- found, pressed, focused, a panel of so
// many points -- and logos-workspace#187 is a path on which all of those are
// true and the screen shows an unchanged conversations pane.
void ShellKeyboardDriver::reportPainted(const QString& name, QQuickItem* item,
                                        QQuickWidget* surface, const QImage& before)
{
    if (!surface || before.isNull()) {
        emit log(QStringLiteral("keyboard: no frame of the app's surface to compare '%1' "
                                "against").arg(name));
        return;
    }
    int looked = 0;
    const int changed = pixelsChangedUnder(item, before, frameOf(surface), &looked);
    if (looked == 0) {
        // An item with no area on screen. Which is itself the answer -- a
        // control a user cannot see is a control that is not there -- so the
        // ancestry says at which level the size was lost.
        emit log(QStringLiteral("WRONG: '%1' covers nothing -- it is %2x%3 in the scene, "
                                "so there is no pixel of it to draw")
                     .arg(name).arg(item->width(), 0, 'f', 0).arg(item->height(), 0, 'f', 0));
        dumpAncestry(item, QStringLiteral("'%1' has no area").arg(name));
        return;
    }
    if (changed == 0) {
        emit log(QStringLiteral("WRONG: '%1' is %2x%3 at (%4, %5) in the app's scene and "
                                "NOT ONE of the %6 pixels under it changed when it opened "
                                "-- the surface draws no trace of it")
                     .arg(name)
                     .arg(item->width(), 0, 'f', 0).arg(item->height(), 0, 'f', 0)
                     .arg(item->mapToScene(QPointF(0, 0)).x(), 0, 'f', 0)
                     .arg(item->mapToScene(QPointF(0, 0)).y(), 0, 'f', 0)
                     .arg(looked));
        dumpAncestry(item, QStringLiteral("'%1' is not drawn").arg(name));
        return;
    }
    emit log(QStringLiteral("keyboard: '%1' IS DRAWN -- %2 of the %3 pixels under it "
                            "changed when it opened").arg(name).arg(changed).arg(looked));
}

void ShellKeyboardDriver::dismiss(QQuickItem* anchor)
{
    QQuickWidget* surface = surfaceOf(anchor);
    if (!surface) return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(surface, &press);
    QCoreApplication::sendEvent(surface, &release);
    settle(300);
}
