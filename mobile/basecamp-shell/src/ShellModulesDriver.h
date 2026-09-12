// The acceptance criterion as a round trip, driven from inside the app.
//
// A simulator has no finger, so the presses below are synthesised -- but only
// the touch is. Everything after it is the real path: the Shell's own
// LogosButton, its MouseArea, ModuleInspectorView's loadRequested /
// unloadRequested, ContentViews' backend call, ShellModulesBackend,
// CoreModuleManager, ICoreRuntime, and the Native container underneath. That
// is the difference between "the Modules tab works" and "the C API works",
// and it is why this drives the BUTTON and reads the ROW back rather than
// asking the runtime twice.
//
// Everything is found by objectName, the handles the views already carry for
// UI automation ("moduleInspector.table", "moduleRow.loadToggle.<name>",
// "settings.section.<key>"). No host type is named on the QML side.
#pragma once

#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <functional>

class BundledSetShellHost;
class QQuickItem;
class QQuickWidget;
class QWidget;

class ShellModulesDriver : public QObject
{
    Q_OBJECT
public:
    ShellModulesDriver(BundledSetShellHost* host, QWidget* shellWidget,
                       QObject* parent = nullptr);

    // Opens Settings -> Module Inspector, checks the rows against the Bundled
    // set, and presses one row's Load/Unload twice. Runs after the first frame:
    // a QML item has no geometry before one, so there is no button to press.
    void run();

signals:
    void log(const QString& line);

private:
    // Every item in every scene the shell owns. Several scenes, because
    // MainContainer puts the sidebar, the content stack and the overlay layer
    // in separate QQuickWidgets.
    //
    // The VISUAL tree, not the QObject tree: QQuickItem::setParentItem does
    // not reparent the QObject, and a view's delegates are created by the
    // delegate model rather than by the contentItem -- so QObject::findChild
    // reaches `moduleInspector.table` and never reaches a single one of its
    // rows. Every handle this driver wants is a delegate.
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
    // Scroll the nearest enclosing flickable so `item` is on screen. Used for
    // the phone's section strip, which is a horizontal scroller BY DESIGN and
    // which a finger would swipe; NOT for a row's action, whose whole
    // requirement is to be reachable without scrolling sideways.
    void scrollIntoView(QQuickItem* item);
    // Spin the event loop until `item` exists or the deadline passes. Delegate
    // creation is asynchronous -- the rows of a view that just became visible
    // do not exist in the same tick.
    QQuickItem* waitFor(const QString& objectName, int timeoutMs);

    BundledSetShellHost* m_host;      // not owned
    QWidget*             m_shell;     // not owned
};
