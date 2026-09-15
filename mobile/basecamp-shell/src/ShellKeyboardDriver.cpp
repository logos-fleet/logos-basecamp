#include "ShellKeyboardDriver.h"

#include "BundledSetShellHost.h"
#include "QuickWidgetKeyboardFocus.h"
#include "ShellSections.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QInputMethod>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QRectF>
#include <QScreen>
#include <QStringList>
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

QString describe(QObject* object);

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
    if (auto* surface = qobject_cast<QQuickWidget*>(object)) {
        QObject* inScene = surface->quickWindow() ? surface->quickWindow()->focusObject()
                                                  : nullptr;
        text += QStringLiteral(" over scene %1, focusing %2")
                    .arg(surface->source().fileName().isEmpty()
                             ? QStringLiteral("<none>")
                             : surface->source().fileName(),
                         inScene && inScene != surface->quickWindow()
                             ? describe(inScene)
                             : QStringLiteral("nothing"));
    }
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

bool ShellKeyboardDriver::hasWork() const
{
    const QStringList apps = m_host->backend()->viewModuleNames();
    return !apps.isEmpty() && !fieldPathFor(apps.first()).field.isEmpty();
}

void ShellKeyboardDriver::run()
{
    const QStringList apps = m_host->backend()->viewModuleNames();
    if (apps.isEmpty()) {
        emit log(QStringLiteral("keyboard: no app in this Bundled set"));
        return;
    }
    const QString app = apps.first();
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
    if (!tap(menuButton)) return;
    settle(kMenuSettleMs);

    // From here on the items are inside Popups, which hang off the scene's
    // overlay rather than off the view's root object.
    QQuickItem* menuItem = waitFor(path.menuItem, 5000, Scope::WithOverlays);
    if (!menuItem) {
        dumpNames(QStringLiteral("the '%1' menu has no '%2'").arg(path.menuButton, path.menuItem));
        return;
    }
    if (!tap(menuItem)) return;

    QQuickItem* field = waitFor(path.field, kDialogBudgetMs, Scope::WithOverlays);
    if (!field) {
        dumpNames(QStringLiteral("no '%1' after '%2'").arg(path.field, path.menuItem));
        return;
    }
    emit log(QStringLiteral("keyboard: '%1' opened the dialog holding '%2'")
                 .arg(path.menuItem, path.field));

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
    // A panel this short is not a keyboard: it is the shortcut bar iOS draws
    // INSTEAD of one while a hardware keyboard is connected -- which on a
    // simulator is a setting, and is the thing logos-workspace#152 asks to rule
    // out. Named here rather than left as a number, because "isVisible=true and
    // nothing to type on" is the confusing case.
    const bool panelIsABarOnly =
        im->isVisible() && !screen.isEmpty() && keyboard.height() < screen.height() / 6;
    if (panelIsABarOnly) {
        emit log(QStringLiteral("keyboard: that is a shortcut bar, not a keyboard -- this "
                                "device has a hardware keyboard connected, so iOS draws no "
                                "panel. The input method is still ON the field"));
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
    } else if (!im->isVisible()) {
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

    dismiss(field);
}

void ShellKeyboardDriver::dismiss(QQuickItem* field)
{
    QQuickWidget* surface = surfaceOf(field);
    if (!surface) return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(surface, &press);
    QCoreApplication::sendEvent(surface, &release);
    settle(300);
}
