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
#include <QStringList>

namespace {

// How long a dialog gets to instantiate. It is QML in the module's own qrc and
// it is created on first open, so this is compilation plus a layout pass.
constexpr int kDialogBudgetMs = 10000;
// A menu opens with a transition; its entries exist before they are pressable.
constexpr int kMenuSettleMs = 400;
// The keyboard is animated up by UIKit, and isVisible() only becomes true once
// it has been reported. Measured on the iPad Air simulator at well under this.
constexpr int kKeyboardBudgetMs = 3000;

QString describe(QObject* object)
{
    if (!object)
        return QStringLiteral("<none>");
    const QString name = object->objectName();
    return QStringLiteral("%1(%2)").arg(QString::fromUtf8(object->metaObject()->className()),
                                        name.isEmpty() ? QStringLiteral("-") : name);
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
    QInputMethod* im = QGuiApplication::inputMethod();
    QElapsedTimer waiting;
    waiting.start();
    while (!im->isVisible() && waiting.elapsed() < kKeyboardBudgetMs)
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
    emit log(QStringLiteral("keyboard: QInputMethod isVisible=%1, panel %2x%3 after %4 ms")
                 .arg(im->isVisible() ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(keyboard.width(), 0, 'f', 0).arg(keyboard.height(), 0, 'f', 0)
                 .arg(waiting.elapsed()));

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
        emit log(QStringLiteral("keyboard: the input method is ON the field and no panel "
                                "is shown -- a connected hardware keyboard does exactly "
                                "this; on a device this would be a platform fault"));
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
