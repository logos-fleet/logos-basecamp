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

#include <QImage>
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

// Whether a scene is one a finger could reach: shown, and with an area to be
// shown in.
//
// THE SHELL CAN HOLD TWO COPIES OF ONE APP'S SCENE (logos-workspace#187).
// Closing a Bundled app takes its widget out of the dock and `deleteLater()`s
// it, and a deferred delete is never delivered while the drivers run -- they
// work off processEvents() inside a QTimer::singleShot and never return to the
// event loop that posted it. So the closed app's surface is still a child of
// the Shell, off screen, with its whole QML scene alive: same handles, same
// bindings, same geometry. A lookup that took the first match took THAT one,
// and the "+" menu it opened, the dialog behind it and the iOS keyboard the
// dialog's field raised were all real and none of them were on the screen.
//
// So every lookup a driver makes is filtered through this. Off screen is not a
// weaker kind of present; it is absent.
bool sceneIsOnScreen(const QQuickWidget* surface);

} // namespace basecamp::shell

class ShellSceneDriver : public QObject
{
    Q_OBJECT
public:
    ShellSceneDriver(QWidget* shellWidget, QObject* parent = nullptr);

signals:
    void log(const QString& line);

protected:
    // How much of a scene a lookup covers. A QtQuick.Controls Popup -- a menu,
    // a dialog -- is NOT a child of the view's root object: it is parented to
    // an Overlay beside it, under the quick window's contentItem. So a menu's
    // entries and a dialog's fields are invisible to a walk that starts at
    // rootObject, and a driver that has to reach one says so.
    enum class Scope {
        Views,          // from each scene's root object, what the view draws
        WithOverlays,   // ...and what is layered over it
    };

    // Every item in every scene the shell owns. Several scenes, because
    // MainContainer puts the sidebar, the content stack and the overlay layer
    // in separate QQuickWidgets -- and a mounted app's view is a fourth.
    //
    // The VISUAL tree, not the QObject tree: QQuickItem::setParentItem does
    // not reparent the QObject, and a view's delegates are created by the
    // delegate model rather than by the contentItem -- so QObject::findChild
    // reaches `moduleInspector.table` and never reaches a single one of its
    // rows. Most handles a driver wants are delegates.
    void forEachItem(const std::function<void(QQuickItem*)>& visit,
                     Scope scope = Scope::Views) const;
    // The item with this objectName in any of those scenes, or nullptr.
    QQuickItem* find(const QString& objectName, Scope scope = Scope::Views) const;
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
    QQuickItem* waitFor(const QString& objectName, int timeoutMs,
                        Scope scope = Scope::Views);
    // Turn the event loop for `ms`, so what just happened is on screen long
    // enough to be seen -- and so the work it queued actually runs.
    void settle(int ms);

    // ── DOES WHAT THE DRIVER PRESSED REACH THE SCREEN? ────────────────────
    //
    // logos-workspace#187: on the venue's physical iPad chat_ui's "+" menu and
    // its New DM dialog are found, pressed, focused and raise the keyboard, and
    // a photograph of the device shows NEITHER of them. Every assertion in this
    // host was about properties, so the whole path reported green while the
    // user saw nothing -- the two below are the pixels.
    //
    // A QQuickWidget's grab RE-RENDERS the scene, so it answers "is this in the
    // frame the surface draws", which is not the same question as "is that
    // frame on the display" and is the half a process can answer about itself.
    QImage frameOf(QQuickWidget* surface) const;
    // Pixels under `item` that differ between two grabs of its surface, and
    // how many were looked at. `looked` is 0 when there was nothing to compare
    // -- no frames, or an item with no area -- which is not the same as "not
    // one pixel changed".
    int pixelsChangedUnder(QQuickItem* item, const QImage& before, const QImage& after,
                           int* looked) const;
    // Every item from `item` up to its scene's root, with the four things that
    // keep one from being drawn: a zero size, `visible`, `opacity` and a
    // clipping ancestor. Printed when an item that should be on screen is not,
    // and it is the whole diagnosis -- the level where the width becomes 0 is
    // the level with the bug.
    void dumpAncestry(QQuickItem* item, const QString& why);

    QWidget* m_shell;  // not owned
};
