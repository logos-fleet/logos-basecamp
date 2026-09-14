#include "ShellSceneDriver.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QRectF>
#include <QVariant>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScreen>
#include <QUrl>
#include <QWidget>

#include <functional>

namespace {

// A press that has to travel through a Flickable and a layout that is still
// settling, in the milliseconds each of those takes. All of them were measured
// on a physical iPad Air (4th gen) -- see settledCentre() and tap().
constexpr int kPollSliceMs      = 20;   // one turn of the loop while waiting
constexpr int kStillForMs       = 250;  // geometry unchanged this long = settled
constexpr int kSettleBudgetMs   = 3000; // give up waiting for it and aim anyway
constexpr int kPressHoldMs      = 80;   // press to release, as a finger would
constexpr int kAfterReleaseMs   = 120;  // let the click's handlers run
constexpr int kAfterScrollMs    = 50;   // let the flickable repaint

// Move one of a flickable's content offsets by the least that brings
// [offset, offset + extent] inside [0, viewport]. Returns whether it moved.
bool scrollAxis(QQuickItem* flickable, const char* property,
                qreal offset, qreal extent, qreal viewport)
{
    const qreal overflow = offset + extent - viewport;
    qreal delta = 0;
    if (overflow > 0)
        delta = overflow;
    else if (offset < 0)
        delta = offset;
    else
        return false;
    flickable->setProperty(property, flickable->property(property).toReal() + delta);
    return true;
}

void walkItems(QQuickItem* item, const std::function<void(QQuickItem*)>& visit)
{
    if (!item) return;
    visit(item);
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children)
        walkItems(child, visit);
}


// The display `w` is on, in the global coordinates mapToGlobal() answers in.
// geometry() rather than availableGeometry(): a point under a notch or a dock
// is still a point a finger reaches, and the question here is reachability,
// not politeness.
QRectF screenRect(const QWidget* w)
{
    const QScreen* screen = w ? w->screen() : nullptr;
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    return screen ? QRectF(screen->geometry()) : QRectF();
}

} // namespace

bool pressIsReachable(const QPointF& inSurface, const QSizeF& surface,
                      const QPointF& onScreen, const QRectF& screen)
{
    if (!QRectF(QPointF(0, 0), surface).contains(inSurface))
        return false;
    if (screen.isEmpty())
        return true;
    return screen.contains(onScreen);
}

ShellSceneDriver::ShellSceneDriver(QWidget* shellWidget, QObject* parent)
    : QObject(parent)
    , m_shell(shellWidget)
{
}

void ShellSceneDriver::dumpNames(const QString& why)
{
    // Every named item in every scene the shell owns. Printed only when a
    // lookup has already failed, and it is the whole diagnosis: a handle that
    // is absent, one that is spelled differently, or a scene this host cannot
    // see into are three different bugs with the same symptom.
    emit log(QStringLiteral("drive: %1 -- named items in the shell's scenes:").arg(why));
    if (!m_shell) return;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        QQuickItem* root = surface->rootObject();
        emit log(QStringLiteral("  scene '%1' (root %2)")
                     .arg(surface->source().toString(),
                          root ? root->objectName() : QStringLiteral("<none>")));
        if (!root) continue;
        QStringList named;
        walkItems(root, [&named](QQuickItem* item) {
            if (!item->objectName().isEmpty())
                named << item->objectName();
        });
        emit log(QStringLiteral("    %1")
                     .arg(named.isEmpty() ? QStringLiteral("(nothing named)")
                                          : named.join(QStringLiteral(", "))));
    }
}

void ShellSceneDriver::forEachItem(const std::function<void(QQuickItem*)>& visit) const
{
    if (!m_shell) return;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>())
        walkItems(surface->rootObject(), visit);
}

QQuickItem* ShellSceneDriver::find(const QString& objectName) const
{
    QQuickItem* found = nullptr;
    forEachItem([&found, &objectName](QQuickItem* item) {
        if (!found && item->objectName() == objectName)
            found = item;
    });
    return found;
}

QQuickWidget* ShellSceneDriver::surfaceOf(QQuickItem* item) const
{
    // By WINDOW, not by walking up to the root object: a QQuickWidget renders
    // into an offscreen QQuickWindow whose contentItem sits ABOVE rootObject,
    // so "walk to the topmost parentItem" lands one level past the root and
    // matches nothing. The shell has four scenes and every item knows which
    // window it is in.
    if (!item || !m_shell) return nullptr;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        if (surface->quickWindow() == item->window())
            return surface;
    }
    return nullptr;
}

void ShellSceneDriver::settle(int ms)
{
    QElapsedTimer since;
    since.start();
    while (since.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

QQuickItem* ShellSceneDriver::waitFor(const QString& objectName, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (QQuickItem* item = find(objectName))
            return item;
        if (t.elapsed() >= timeoutMs)
            return nullptr;
        QCoreApplication::processEvents(QEventLoop::AllEvents, kPollSliceMs);
    }
}

QPointF ShellSceneDriver::settledCentre(QQuickItem* item)
{
    // Unchanged for kStillForMs of WALL CLOCK, not for N turns of the loop:
    // processEvents returns the moment the queue is empty, so a "stable for
    // eight reads" rule can be satisfied in microseconds while the next polish
    // pass is still pending. Observed on the iPad: a first press at x=190 --
    // the row's left edge, where the toggle sat before the table's columns took
    // their widths -- then a second, a second later, at the real x=826.
    const auto centreOf = [](QQuickItem* i) {
        return i->mapToScene(QPointF(i->width() / 2.0, i->height() / 2.0));
    };
    QElapsedTimer budget;
    budget.start();
    QElapsedTimer still;
    still.start();
    QPointF previous = centreOf(item);
    while (budget.elapsed() < kSettleBudgetMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, kPollSliceMs);
        const QPointF centre = centreOf(item);
        if (centre != previous) {
            previous = centre;
            still.restart();
            continue;
        }
        if (still.elapsed() >= kStillForMs)
            return centre;
    }
    return previous;
}

void ShellSceneDriver::scrollIntoView(QQuickItem* item)
{
    if (!item) return;
    // The nearest ancestor that has a contentX is the flickable this item
    // rides in -- QQuickItem has no such property, Flickable and every view
    // built on it does. Asking by property rather than by type keeps this off
    // Qt's private headers.
    for (QQuickItem* p = item->parentItem(); p; p = p->parentItem()) {
        if (!p->property("contentX").isValid()) continue;
        // BOTH axes, because the two callers scroll in different directions:
        // the phone's section strip sideways, the sidebar's app column down.
        // A flickable answers to contentX and contentY whichever way it
        // flicks, and the axis with nothing to correct costs a comparison.
        const QPointF topLeft = item->mapToItem(p, QPointF(0, 0));
        const bool movedX =
            scrollAxis(p, "contentX", topLeft.x(), item->width(), p->width());
        const bool movedY =
            scrollAxis(p, "contentY", topLeft.y(), item->height(), p->height());
        if (movedX || movedY)
            QCoreApplication::processEvents(QEventLoop::AllEvents, kAfterScrollMs);
        return;
    }
}

bool ShellSceneDriver::tap(QQuickItem* item)
{
    QQuickWidget* surface = surfaceOf(item);
    if (!surface) {
        emit log(QStringLiteral("drive: '%1' is in no scene this host owns")
                     .arg(item ? item->objectName() : QString()));
        return false;
    }
    // Scene coordinates ARE widget coordinates for a QQuickWidget, so the
    // centre of the item in the scene is where the press goes -- once the
    // layout has stopped moving it there.
    const QPointF centre = settledCentre(item);

    // ...but only if a FINGER could have landed there. Qt delivers a press by
    // COORDINATE, so a point the display does not show is not "a press on a
    // control that is out of the way", it is a press on whatever is at that
    // coordinate -- which is silence, and looks exactly like a button that
    // does nothing.
    //
    // Reported, not worked around: the Settings views collapse to the row and
    // its action on a narrow screen (logos-workspace#84), so a control that
    // cannot be reached is a layout regression, and a driver that activated it
    // by hand instead would keep this verdict green while nobody could load a
    // module at all. That is what it used to do.
    //
    // BOTH rectangles, because the pane containing the control proves nothing
    // about the phone containing the pane -- see pressIsReachable().
    const QPointF global = surface->mapToGlobal(centre);
    const QRectF screen = screenRect(surface);
    if (!pressIsReachable(centre, QSizeF(surface->size()), global, screen)) {
        emit log(QStringLiteral("WRONG: '%1' is at (%2, %3) of a %4x%5 view, "
                                "at (%6, %7) on a %8x%9 screen "
                                "-- no touch can reach it on this screen")
                     .arg(item->objectName())
                     .arg(centre.x(), 0, 'f', 0).arg(centre.y(), 0, 'f', 0)
                     .arg(surface->width()).arg(surface->height())
                     .arg(global.x(), 0, 'f', 0).arg(global.y(), 0, 'f', 0)
                     .arg(screen.width(), 0, 'f', 0).arg(screen.height(), 0, 'f', 0));
        return false;
    }

    emit log(QStringLiteral("drive: press '%1' at (%2, %3) in %4x%5")
                 .arg(item->objectName())
                 .arg(centre.x(), 0, 'f', 0).arg(centre.y(), 0, 'f', 0)
                 .arg(surface->width()).arg(surface->height()));

    QMouseEvent press(QEvent::MouseButtonPress, centre, global,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, centre, global,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QGuiApplication::sendEvent(surface, &press);
    // A gap between the two, and an event loop turn to spend it in. The table
    // rides in a Flickable, and a Flickable does not hand a press straight to
    // the child under it -- it holds it until the gesture has declared itself,
    // then replays press and release together. Sent back to back in one turn
    // that replay is a coin flip: the row's control got the pair on some runs
    // and nothing at all on others. A finger takes about this long.
    QCoreApplication::processEvents(QEventLoop::AllEvents, kPressHoldMs);
    QGuiApplication::sendEvent(surface, &release);
    QCoreApplication::processEvents(QEventLoop::AllEvents, kAfterReleaseMs);
    return true;
}
