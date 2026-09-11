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
#include <QString>
#include <QStringList>

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
    // The item with this objectName anywhere under the shell's QQuickWidgets,
    // or nullptr. Several widgets, because MainContainer puts the sidebar, the
    // content stack and the overlay layer in separate scenes.
    QQuickItem* find(const QString& objectName) const;
    QQuickWidget* surfaceOf(QQuickItem* item) const;
    // A press and a release at the item's centre, entering the scene where a
    // finger's would.
    bool tap(QQuickItem* item);
    // Spin the event loop until `item` exists or the deadline passes. Delegate
    // creation is asynchronous -- the rows of a view that just became visible
    // do not exist in the same tick.
    QQuickItem* waitFor(const QString& objectName, int timeoutMs);

    BundledSetShellHost* m_host;      // not owned
    QWidget*             m_shell;     // not owned
};
