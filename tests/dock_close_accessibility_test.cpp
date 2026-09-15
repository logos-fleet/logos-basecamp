// srcdeps: WorkspaceArea.cpp
//
// CLOSING AN APP MUST NOT ANNOUNCE A HALF-DESTROYED WIDGET (#139).
//
// The mobile Shell segfaulted on iOS every time a user left a web app, about
// 19 s into every scripted run, and the fault was inside `~QDockWidget`:
//
//     QAbstractButton::isCheckable() const
//     QAccessibleButton::role() const
//     QIOSPlatformAccessibility::notifyAccessibilityUpdate(QAccessibleEvent*)
//     QAccessibleCache::sendObjectDestroyedEvent(QObject*)
//     QWidget::~QWidget()
//     QDockWidgetTitleButton::~QDockWidgetTitleButton()
//
// The mechanism is entirely Qt's and has nothing to do with web apps:
//
//   1. `QDockWidget` always owns two `QDockWidgetTitleButton`s -- Qt creates
//      the float and close buttons in `QDockWidgetPrivate::init()` whether or
//      not a custom title bar hides them;
//   2. iOS puts every visible widget into `QAccessibleCache` the first time
//      UIKit asks a `QUIView` for its accessibility elements, which also
//      switches accessibility ON for the rest of the process;
//   3. `QWidget::~QWidget()` posts an `ObjectDestroyed` update for anything
//      the cache holds -- and by then the `QAbstractButton` half of the button
//      has already been destroyed;
//   4. Qt's iOS plugin answers that update by asking the interface for its
//      `role()`, which for a button reads `isCheckable()` off the corpse.
//
// Only step (4) is iOS-specific, and only step (3) is ours to prevent: a widget
// the cache does not hold is never announced. So the Shell drops a dock and
// everything under it from the accessibility cache at the last instant before
// the dock is destroyed, and this asserts the property that makes the crash
// impossible -- NO `ObjectDestroyed` update is ever delivered for an interface
// that is already invalid.
//
// It runs on any platform: `QAccessible::setActive(true)` arms step (3)
// everywhere, and an update handler stands in for the iOS plugin so the
// assertion is about what Qt announces rather than about what UIKit does with
// it.
//
//   nix build .#unit-tests -L
#include "WorkspaceArea.h"

#include <QtTest/QtTest>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QPointer>
#include <QStringList>

namespace {

// Let QTimer::singleShot(0, ...) callbacks and deferred deletes run --
// WorkspaceArea uses both around every add and remove.
void processDeferred()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

// What the iOS plugin is handed. A file-static because
// QAccessible::UpdateHandler is a plain function pointer.
struct DestroyedUpdates
{
    int announced = 0;
    QStringList invalid;   // the ones that would have crashed the phone
};
DestroyedUpdates g_updates;

QString describe(QAccessibleInterface* iface)
{
    QObject* obj = iface->object();
    if (!obj) return QStringLiteral("(no object)");
    const QString name = obj->objectName();
    // metaObject() is virtual and the vptr has already been walked back to
    // QWidget's by the time this runs, so the class name is the base's -- the
    // objectName is what identifies Qt's own title buttons.
    return name.isEmpty() ? QString::fromLatin1(obj->metaObject()->className()) : name;
}

void recordUpdate(QAccessibleEvent* event)
{
    if (event->type() != QAccessible::ObjectDestroyed) return;
    QAccessibleInterface* iface = event->accessibleInterface();
    if (!iface) return;
    ++g_updates.announced;
    // EXACTLY WHAT THE iOS PLUGIN SKIPS. It warns "invalid accessible
    // interface" and then calls role() on this same pointer anyway; role() on
    // a QAccessibleButton dereferences the QAbstractButton that ~QWidget is
    // already past. Asking isValid() here is reading the state that makes that
    // call fatal, without making the test itself crash.
    if (!iface->isValid()) g_updates.invalid << describe(iface);
}

// Everything UIKit's first accessibility query puts in the cache: an interface
// per visible widget under the window (QUIView::initAccessibility walks the
// accessible root exactly this way).
void cacheInterfacesFor(QWidget* root)
{
    QAccessible::queryAccessibleInterface(root);
    for (QWidget* child : root->findChildren<QWidget*>())
        QAccessible::queryAccessibleInterface(child);
}

QWidget* makePluginWidget(const QString& label)
{
    auto* w = new QLabel(label);
    w->setObjectName("pluginWidget_" + label);
    w->setAttribute(Qt::WA_DontShowOnScreen);
    return w;
}

} // namespace

class DockCloseAccessibilityTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void closingAnAppAnnouncesNoDestroyedWidget();
    void closingOneAppLeavesTheOtherReachable();

private:
    QAccessible::UpdateHandler m_previousHandler = nullptr;
};

void DockCloseAccessibilityTest::init()
{
    g_updates = {};
    // Step (3) of the note above: without this ~QWidget says nothing and the
    // assertion below would pass for the wrong reason. There is always a
    // QPlatformAccessibility (QPlatformIntegration::accessibility() creates a
    // default one), so this holds on offscreen too.
    QAccessible::setActive(true);
    m_previousHandler = QAccessible::installUpdateHandler(recordUpdate);
}

void DockCloseAccessibilityTest::cleanup()
{
    QAccessible::installUpdateHandler(m_previousHandler);
    QAccessible::setActive(false);
}

void DockCloseAccessibilityTest::closingAnAppAnnouncesNoDestroyedWidget()
{
    QVERIFY2(QAccessible::isActive(),
             "accessibility could not be armed -- the assertion below would be vacuous");

    WorkspaceArea area;
    area.addPluginDock(makePluginWidget("app"), QStringLiteral("my_app"),
                       QStringLiteral("My App"));
    processDeferred();

    QDockWidget* dock = area.dockFor(QStringLiteral("my_app"));
    QVERIFY(dock);
    // Qt's own float and close buttons, which exist whatever the title bar
    // shows -- these are the objects the phone died on.
    QVERIFY(!dock->findChildren<QAbstractButton*>().isEmpty());

    cacheInterfacesFor(&area);

    QPointer<QDockWidget> alive(dock);
    area.removePluginDock(QStringLiteral("my_app"));
    processDeferred();
    QVERIFY2(alive.isNull(), "the dock was not destroyed, so nothing was proven");

    QVERIFY2(g_updates.invalid.isEmpty(),
             qPrintable(QStringLiteral("closing an app announced %1 destroyed widget(s) "
                                       "whose accessible interface was already invalid: %2 "
                                       "-- Qt's iOS plugin calls role() on each of these")
                            .arg(g_updates.invalid.size())
                            .arg(g_updates.invalid.join(QStringLiteral(", ")))));
}

// The dock that STAYS keeps its accessibility. Dropping the whole cache, or
// turning accessibility off for the process, would pass the test above and
// leave a screen reader with nothing to read.
void DockCloseAccessibilityTest::closingOneAppLeavesTheOtherReachable()
{
    WorkspaceArea area;
    area.addPluginDock(makePluginWidget("one"), QStringLiteral("app_one"));
    area.addPluginDock(makePluginWidget("two"), QStringLiteral("app_two"));
    processDeferred();

    cacheInterfacesFor(&area);

    area.removePluginDock(QStringLiteral("app_one"));
    processDeferred();

    QDockWidget* survivor = area.dockFor(QStringLiteral("app_two"));
    QVERIFY(survivor);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(survivor);
    QVERIFY2(iface && iface->isValid(),
             "the surviving dock lost its accessible interface");
}

QTEST_MAIN(DockCloseAccessibilityTest)
#include "dock_close_accessibility_test.moc"
