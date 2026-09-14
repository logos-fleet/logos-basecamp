#include "WebAppSurface.h"

#include <QEvent>
#include <QMetaObject>

WebAppSurface::WebAppSurface(QString moduleName, QWidget* parent)
    : QWidget(parent)
    , m_moduleName(std::move(moduleName))
{
    setObjectName(QStringLiteral("webAppSurface.%1").arg(m_moduleName));
    // A dock whose widget has no size of its own collapses to nothing, and a
    // page inset to nothing is a page the user cannot see -- which reads
    // exactly like the module failing to come up.
    setMinimumSize(64, 64);
    // NO BACKGROUND OF ITS OWN, and no translucency either. The page is a
    // native view in front of Qt's whole surface, so what this widget draws is
    // covered whenever the app is open -- but in the frames BEFORE the page
    // comes forward it is not, and a hole punched through the window is a black
    // rectangle rather than the Shell. Inheriting the workspace's own
    // background is what makes those frames look like the Shell.
    watchAncestors();
}

QRect WebAppSurface::pageRect() const
{
    const QWidget* top = window();
    if (!top || !isVisible() || width() <= 0 || height() <= 0) return {};
    // CLIPPED TO THE WINDOW, and that is not belt-and-braces. The Shell's
    // content area can be LARGER than the window it is in -- on an iPhone 16 Pro
    // the workspace dock measures 703x739 inside a 402x874 window, and a
    // `ui_qml` app's widget simply has its overflow left undrawn. A native page
    // has no such luck: positioned at 703 wide it is 300 px of module UI past
    // the right edge of the screen with no way to scroll to it. The hole the
    // user can actually see is the intersection, and that is the hole the page
    // gets.
    return QRect(mapTo(top, QPoint(0, 0)), size()).intersected(top->rect());
}

bool WebAppSurface::onScreen() const
{
    // Derived from the rect rather than from isVisible() alone: a placeholder
    // the Shell has scrolled or laid out entirely off the window is visible to
    // Qt and is not somewhere a page can be put.
    return !pageRect().isEmpty();
}

void WebAppSurface::watchAncestors()
{
    for (const QPointer<QWidget>& watched : m_watched) {
        if (watched) watched->removeEventFilter(this);
    }
    m_watched.clear();
    for (QWidget* ancestor = parentWidget(); ancestor;
         ancestor = ancestor->parentWidget()) {
        ancestor->installEventFilter(this);
        m_watched.append(ancestor);
    }
}

void WebAppSurface::announce()
{
    const QRect rect = pageRect();
    const bool up = onScreen();
    if (m_announced && rect == m_lastRect && up == m_lastOnScreen) return;
    m_lastRect = rect;
    m_lastOnScreen = up;
    m_announced = true;
    emit placementChanged(m_moduleName, rect, up);
}

void WebAppSurface::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    announce();
}

void WebAppSurface::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    announce();
}

void WebAppSurface::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    announce();
}

void WebAppSurface::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    announce();
}

bool WebAppSurface::event(QEvent* event)
{
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::ParentChange) {
        watchAncestors();
        announce();
    }
    return handled;
}

bool WebAppSurface::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
        // DEFERRED, because a filter runs BEFORE the widget it watches: the
        // ancestor has not re-run its layout yet, so this surface's own
        // geometry is still the one it is about to leave. Announcing on the
        // next turn reads the layout that actually happened -- and costs
        // nothing when the surface's own move or resize event got there first,
        // since announce() is silent when the answer has not changed.
        QMetaObject::invokeMethod(this, [this] { announce(); }, Qt::QueuedConnection);
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
