// Driving the Shell's rendered scenes from inside the app.
//
// A simulator has no finger, so the presses are synthesised -- but only the
// touch is. Everything after it is the real path: the Shell's own controls,
// their MouseAreas, the signals they emit and the backend they land on. Every
// driver needs exactly this and nothing else (ShellModulesDriver for the
// Modules tab, ShellAppDriver for a Bundled app, ShellWebAppDriver for a `web`
// one, ShellCatalogDriver for an installed package), and a second copy of
// "settle, then press at the centre" would be a second set of timings to get
// wrong -- every constant here was measured on a physical iPad Air (4th gen).
//
// Everything is found by objectName, the handles the views already carry for
// UI automation. No host type is named on the QML side.
#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <functional>

class QQuickItem;
class QQuickWidget;
class QWidget;

namespace basecamp::shell {

// Whether a synthesised press is one a FINGER could have made. Two questions,
// and the second is not implied by the first:
//
//   1. does the control's own surface show `inSurface`? (a row scrolled out of
//      its table, logos-workspace#84)
//   2. does the screen show where that lands, `onScreen`?
//
// (2) exists because a Qt layout handed less room than its minimum does not
// shrink, it OVERFLOWS: on the iPhone 16 Pro the Shell laid itself out 784 pt
// wide on a 402-pt screen, so the Modules row's toggle was well inside its own
// 704-pt pane and ~300 pt past the edge of the phone (logos-workspace#87). A
// driver that asks only (1) presses it by coordinate -- which is the one thing
// a person cannot do -- and reports the tab green.
//
// An empty `screen` (nothing to ask) leaves (1) as the whole answer.
bool pressIsReachable(const QPointF& inSurface, const QSizeF& surface,
                      const QPointF& onScreen, const QRectF& screen);

} // namespace basecamp::shell

class ShellSceneDriver : public QObject
{
    Q_OBJECT
public:
    ShellSceneDriver(QWidget* shellWidget, QObject* parent = nullptr);

signals:
    void log(const QString& line);

protected:
    // Every item in every scene the shell owns. Several scenes, because
    // MainContainer puts the sidebar, the content stack and the overlay layer
    // in separate QQuickWidgets -- and a mounted app's view is a fourth.
    //
    // The VISUAL tree, not the QObject tree: QQuickItem::setParentItem does
    // not reparent the QObject, and a view's delegates are created by the
    // delegate model rather than by the contentItem -- so QObject::findChild
    // reaches `moduleInspector.table` and never reaches a single one of its
    // rows. Most handles a driver wants are delegates.
    void forEachItem(const std::function<void(QQuickItem*)>& visit) const;
    // The item with this objectName in any of those scenes, or nullptr.
    QQuickItem* find(const QString& objectName) const;
    // Every named item in every scene, for when a lookup failed.
    void dumpNames(const QString& why);
    QQuickWidget* surfaceOf(QQuickItem* item) const;
    // A press and a release at the item's centre, entering the scene where a
    // finger's would. Fails, rather than working around it, when that centre
    // is off the viewport -- see the comment in tap().
    bool tap(QQuickItem* item);
    // The item's centre in scene coordinates, once it has stopped moving.
    // Geometry lands over several polish passes -- a Settings panel's width
    // cascades StackLayout -> ColumnLayout -> table -> Flickable -> ListView
    // -> row -- and a press aimed at where a control was two passes ago lands
    // on nothing at all, which reads exactly like a button that does not work.
    QPointF settledCentre(QQuickItem* item);
    // Scroll the nearest enclosing flickable, on either axis, so `item` is on
    // screen. Used for the surfaces that scroll BY DESIGN and that a finger
    // would swipe -- the phone's section strip sideways, the sidebar's app
    // column down; NOT for a row's action, whose whole requirement is to be
    // reachable without scrolling at all.
    void scrollIntoView(QQuickItem* item);
    // Spin the event loop until `item` exists or the deadline passes. Delegate
    // creation is asynchronous -- the rows of a view that just became visible
    // do not exist in the same tick.
    QQuickItem* waitFor(const QString& objectName, int timeoutMs);
    // Turn the event loop for `ms`, so what just happened is on screen long
    // enough to be seen -- and so the work it queued actually runs.
    void settle(int ms);

    QWidget* m_shell;  // not owned
};
