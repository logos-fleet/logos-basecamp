#include "QuickWidgetKeyboardFocus.h"

#include <QCoreApplication>
#include <QEvent>
#include <QInputMethodQueryEvent>
#include <QQuickWidget>
#include <QQuickWindow>

QuickWidgetKeyboardFocus::QuickWidgetKeyboardFocus(QObject* parent)
    : QObject(parent)
{
}

void QuickWidgetKeyboardFocus::watchEverything()
{
    // On the APPLICATION, because an event filter installed on a parent widget
    // does not see its children's events -- and the surfaces this is for do not
    // exist yet when it is installed.
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
}

bool QuickWidgetKeyboardFocus::eventFilter(QObject* watched, QEvent* event)
{
    // The type first and the cast second: this filter sees every event in the
    // process, and a qobject_cast per event is a cost with no reason.
    if (event->type() == QEvent::Show) {
        if (auto* surface = qobject_cast<QQuickWidget*>(watched))
            watch(surface);
    }
    return false;
}

void QuickWidgetKeyboardFocus::watch(QQuickWidget* surface)
{
    if (!surface || m_watched.contains(surface))
        return;
    m_watched.insert(surface);
    connect(surface, &QObject::destroyed, this,
            [this](QObject* gone) { m_watched.remove(gone); });
    connect(surface->quickWindow(), &QQuickWindow::focusObjectChanged, this,
            [this, surface](QObject* focusObject) {
                if (acceptsInputMethod(focusObject))
                    takeKeyboardFocus(surface);
            });
    reconcile(surface);
}

void QuickWidgetKeyboardFocus::reconcile(QQuickWidget* surface)
{
    // A scene can have focused its field before anyone was listening -- the QML
    // is loaded and shown in one go, and a dialog that opens with a focused
    // field emits its change in between. Asking once here is what makes watch()
    // total: after it, the invariant holds rather than holding from the next
    // change onwards.
    if (acceptsInputMethod(surface->quickWindow()->focusObject()))
        takeKeyboardFocus(surface);
}

bool QuickWidgetKeyboardFocus::acceptsInputMethod(QObject* object)
{
    if (!object)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(object, &query);
    return query.value(Qt::ImEnabled).toBool();
}

bool QuickWidgetKeyboardFocus::takeKeyboardFocus(QQuickWidget* surface)
{
    if (!surface)
        return false;
    QWidget* window = surface->window();
    if (!window)
        return false;
    if (window->focusWidget() == surface)
        return true;
    if (surface->focusPolicy() == Qt::NoFocus)
        return false;
    // MouseFocusReason: this is the focus the press would have carried. The
    // reason is not cosmetic -- Qt::TabFocusReason would make QQuickWidget move
    // the scene's focus to its first item, taking it off the field the user
    // just tapped.
    surface->setFocus(Qt::MouseFocusReason);
    return window->focusWidget() == surface;
}
