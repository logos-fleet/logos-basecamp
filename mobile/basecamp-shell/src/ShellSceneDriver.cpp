#include "ShellSceneDriver.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QRectF>
#include <QVariant>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
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

// Where a walk of one surface starts. The quick window's contentItem is the
// WHOLE scene -- the root object AND the Overlay a Popup (a menu, a dialog) is
// parented to; the root object alone is only what the view draws. Only a lookup
// that has to reach inside a popup asks for the wider one, because everything
// else is better off not seeing an item a closed popup still holds.
QQuickItem* walkRoot(QQuickWidget* surface, bool withOverlays)
{
    if (withOverlays && surface->quickWindow())
        return surface->quickWindow()->contentItem();
    return surface->rootObject();
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

namespace basecamp::shell {

bool sceneIsOnScreen(const QQuickWidget* surface)
{
    // isVisible() is the whole question and it already covers the ancestors: a
    // widget in a dock the workspace has removed, or in a section the content
    // stack is not showing, answers false without this having to know about
    // either. The size is the second half of the same thing -- a surface laid
    // out to nothing draws nothing.
    return surface && surface->isVisible() && !surface->size().isEmpty();
}

bool pressIsReachable(const QPointF& inSurface, const QSizeF& surface,
                      const QPointF& onScreen, const QRectF& screen)
{
    if (!QRectF(QPointF(0, 0), surface).contains(inSurface))
        return false;
    if (screen.isEmpty())
        return true;
    return screen.contains(onScreen);
}

} // namespace basecamp::shell

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
        // OFF-SCREEN SCENES ARE LISTED AND MARKED. A lookup does not see them
        // (forEachItem below), so when one fails the reader has to be told that
        // the handle exists in a scene nobody can press -- an unmounted app
        // still waiting for its deferred delete looks otherwise exactly like a
        // handle that was never there.
        const bool onScreen = basecamp::shell::sceneIsOnScreen(surface);
        emit log(QStringLiteral("  scene '%1' (root %2)%3")
                     .arg(surface->source().toString(),
                          root ? root->objectName() : QStringLiteral("<none>"),
                          onScreen ? QString()
                                   : QStringLiteral("  -- NOT ON SCREEN, no lookup "
                                                    "reaches it")));
        if (!root) continue;
        QStringList named;
        // The whole scene, overlays included: a handle a driver could not find
        // because it is inside a menu that never opened is a different bug
        // from one that is spelled differently, and this is where that is told.
        walkItems(walkRoot(surface, /*withOverlays=*/true), [&named](QQuickItem* item) {
            if (!item->objectName().isEmpty())
                named << item->objectName();
        });
        emit log(QStringLiteral("    %1")
                     .arg(named.isEmpty() ? QStringLiteral("(nothing named)")
                                          : named.join(QStringLiteral(", "))));
    }
}

void ShellSceneDriver::forEachItem(const std::function<void(QQuickItem*)>& visit,
                                   Scope scope) const
{
    if (!m_shell) return;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        // The scenes a finger could reach, and no others -- see
        // sceneIsOnScreen() for the two chat_ui scenes that made this the rule.
        if (!basecamp::shell::sceneIsOnScreen(surface))
            continue;
        walkItems(walkRoot(surface, scope == Scope::WithOverlays), visit);
    }
}

QQuickItem* ShellSceneDriver::find(const QString& objectName, Scope scope) const
{
    QQuickItem* found = nullptr;
    forEachItem([&found, &objectName](QQuickItem* item) {
        if (!found && item->objectName() == objectName)
            found = item;
    }, scope);
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

QQuickItem* ShellSceneDriver::waitFor(const QString& objectName, int timeoutMs,
                                      Scope scope)
{
    QElapsedTimer t;
    t.start();
    for (;;) {
        if (QQuickItem* item = find(objectName, scope))
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

// WHERE A PRESS ON THIS ITEM WOULD GO, and whether a finger could have made it.
//
// Both halves of the question tap() has to answer before it presses anything,
// and the whole of the one pressWouldReach() answers instead of pressing -- the
// catalog page's Install control starts a download and an install, and "the
// control is where a finger can reach it" is a claim about the LAYOUT that a
// driver has to be able to make without buying the consequence. One copy, so a
// reachability verdict reads the same sentence wherever it came from.
std::optional<ShellSceneDriver::Press> ShellSceneDriver::resolvePress(QQuickItem* item)
{
    QQuickWidget* surface = surfaceOf(item);
    if (!surface) {
        emit log(QStringLiteral("drive: '%1' is in no scene this host owns")
                     .arg(item ? item->objectName() : QString()));
        return {};
    }
    // ...and one that is on the screen. A lookup already refuses an off-screen
    // scene, so this only catches an item that went off screen between being
    // found and being pressed -- but a press into a surface nobody can see is
    // the failure this driver exists to report, so it is checked where the
    // press happens rather than only where the item came from.
    if (!basecamp::shell::sceneIsOnScreen(surface)) {
        emit log(QStringLiteral("WRONG: '%1' is in a scene that is not on screen "
                                "-- no touch can reach it")
                     .arg(item->objectName()));
        return {};
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
    if (!basecamp::shell::pressIsReachable(centre, QSizeF(surface->size()), global, screen)) {
        emit log(QStringLiteral("WRONG: '%1' is at (%2, %3) of a %4x%5 view, "
                                "at (%6, %7) on a %8x%9 screen "
                                "-- no touch can reach it on this screen")
                     .arg(item->objectName())
                     .arg(centre.x(), 0, 'f', 0).arg(centre.y(), 0, 'f', 0)
                     .arg(surface->width()).arg(surface->height())
                     .arg(global.x(), 0, 'f', 0).arg(global.y(), 0, 'f', 0)
                     .arg(screen.width(), 0, 'f', 0).arg(screen.height(), 0, 'f', 0));
        return {};
    }
    return Press{ surface, centre, global };
}

bool ShellSceneDriver::pressWouldReach(QQuickItem* item)
{
    return resolvePress(item).has_value();
}

bool ShellSceneDriver::tap(QQuickItem* item)
{
    const std::optional<Press> press = resolvePress(item);
    if (!press)
        return false;
    QQuickWidget* surface = press->surface;
    const QPointF centre = press->centre;
    const QPointF global = press->global;

    emit log(QStringLiteral("drive: press '%1' at (%2, %3) in %4x%5")
                 .arg(item->objectName())
                 .arg(centre.x(), 0, 'f', 0).arg(centre.y(), 0, 'f', 0)
                 .arg(surface->width()).arg(surface->height()));

    QMouseEvent pressEvent(QEvent::MouseButtonPress, centre, global,
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent releaseEvent(QEvent::MouseButtonRelease, centre, global,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QGuiApplication::sendEvent(surface, &pressEvent);
    // A gap between the two, and an event loop turn to spend it in. The table
    // rides in a Flickable, and a Flickable does not hand a press straight to
    // the child under it -- it holds it until the gesture has declared itself,
    // then replays press and release together. Sent back to back in one turn
    // that replay is a coin flip: the row's control got the pair on some runs
    // and nothing at all on others. A finger takes about this long.
    QCoreApplication::processEvents(QEventLoop::AllEvents, kPressHoldMs);
    QGuiApplication::sendEvent(surface, &releaseEvent);
    QCoreApplication::processEvents(QEventLoop::AllEvents, kAfterReleaseMs);
    return true;
}

QImage ShellSceneDriver::frameOf(QQuickWidget* surface) const
{
    // grabFramebuffer() rather than QWidget::grab(): a QQuickWidget's pixels
    // are its scene's, rendered by the scene graph, and the widget's backing
    // store knows nothing about them.
    return surface ? surface->grabFramebuffer() : QImage();
}

int ShellSceneDriver::pixelsChangedUnder(QQuickItem* item, const QImage& before,
                                         const QImage& after, int* looked) const
{
    if (looked) *looked = 0;
    QQuickWidget* surface = surfaceOf(item);
    if (!surface || before.isNull() || after.isNull() || before.size() != after.size())
        return 0;
    const QSizeF logicalSurface(surface->size());
    if (logicalSurface.isEmpty())
        return 0;
    // The grabs are in DEVICE pixels and the item's geometry is in logical
    // ones, so the rectangle is scaled by the ratio the grabs themselves
    // report -- a surface that renders at 2x and one that renders at 1x both
    // answer correctly, with no devicePixelRatio read from anywhere else.
    const qreal scaleX = before.width()  / logicalSurface.width();
    const qreal scaleY = before.height() / logicalSurface.height();
    const QPointF topLeft = item->mapToScene(QPointF(0, 0));
    const QRect box = QRectF(topLeft.x() * scaleX, topLeft.y() * scaleY,
                             item->width() * scaleX, item->height() * scaleY)
                          .toRect()
                          .intersected(QRect(QPoint(0, 0), before.size()));
    if (box.isEmpty())
        return 0;
    const QImage a = before.convertToFormat(QImage::Format_ARGB32);
    const QImage b = after.convertToFormat(QImage::Format_ARGB32);
    int changed = 0;
    for (int y = box.top(); y <= box.bottom(); ++y) {
        const QRgb* rowA = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* rowB = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = box.left(); x <= box.right(); ++x)
            if (rowA[x] != rowB[x])
                ++changed;
    }
    if (looked) *looked = box.width() * box.height();
    return changed;
}

void ShellSceneDriver::dumpAncestry(QQuickItem* item, const QString& why)
{
    emit log(QStringLiteral("drive: %1 -- from the item up to its scene:").arg(why));
    if (!item) {
        emit log(QStringLiteral("  (no item)"));
        return;
    }
    for (QQuickItem* i = item; i; i = i->parentItem()) {
        const QPointF at = i->mapToScene(QPointF(0, 0));
        emit log(QStringLiteral("  %1(%2) %3x%4 at (%5, %6) visible=%7 opacity=%8%9")
                     .arg(QString::fromUtf8(i->metaObject()->className()),
                          i->objectName().isEmpty() ? QStringLiteral("-") : i->objectName())
                     .arg(i->width(), 0, 'f', 0).arg(i->height(), 0, 'f', 0)
                     .arg(at.x(), 0, 'f', 0).arg(at.y(), 0, 'f', 0)
                     .arg(i->isVisible() ? QStringLiteral("true") : QStringLiteral("false"))
                     .arg(i->opacity(), 0, 'f', 2)
                     .arg(i->clip() ? QStringLiteral(" clip") : QString()));
    }
}
