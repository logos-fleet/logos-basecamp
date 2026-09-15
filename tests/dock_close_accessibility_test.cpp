// srcdeps: WorkspaceArea.cpp
//
// CLOSING AN APP MUST NOT ANNOUNCE A HALF-DESTROYED WIDGET (#139).
//
// The mobile Shell segfaulted on iOS every time a user left a web app, about
// 19 s into every scripted run, inside `~QDockWidget`: `QWidget::~QWidget()`
// announces an `ObjectDestroyed` for every widget the accessibility cache still
// holds, and Qt's iOS plugin answers that by asking the interface for its
// `role()` -- which for one of the two `QDockWidgetTitleButton`s every dock owns
// reads `isCheckable()` off the `QAbstractButton` half that ~QWidget is already
// past. `forgetAccessibility()` in src/WorkspaceArea.cpp carries the full
// mechanism and the backtrace.
//
// Only the last step is iOS-specific, and only the announcement is ours to
// prevent: a widget the cache does not hold is never announced. So the Shell
// drops a dock and everything under it from the accessibility cache at the last
// instant before the dock is destroyed, and these cases assert the property
// that makes the crash impossible -- NO `ObjectDestroyed` update is ever
// delivered for an interface that is already invalid.
//
// THREE CASES, because only one of them can be proven everywhere.
//
// `theAppsWidgetLeavesTheCacheBeforeItsDestructorRuns` is the mechanism, and it
// needs nothing from the platform: a widget the app owns asks, from inside its
// own destructor, whether the cache still holds it. The answer is the whole fix
// -- "no" means ~QWidget has nothing to announce a moment later.
//
// `closingAnAppAnnouncesNoDestroyedWidget` is the crash itself, one step
// removed: it arms Qt's half with `QAccessible::setActive(true)` and stands an
// update handler in for the iOS plugin. That only works where the platform
// plugin provides a QPlatformAccessibility -- Qt 6.11's default does, Qt 6.9's
// offscreen does not -- so it skips rather than passing vacuously.
//
// `closingOneAppLeavesTheOtherReachable` is the other half of the fix: what the
// dock that STAYS must keep.
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

// The destroyed-widget announcements that would have crashed the phone. A
// file-static because QAccessible::UpdateHandler is a plain function pointer.
QStringList g_invalidAnnouncements;

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
    // EXACTLY WHAT THE iOS PLUGIN SKIPS. It warns "invalid accessible
    // interface" and then calls role() on this same pointer anyway; role() on
    // a QAccessibleButton dereferences the QAbstractButton that ~QWidget is
    // already past. Asking isValid() here is reading the state that makes that
    // call fatal, without making the test itself crash.
    if (!iface->isValid()) g_invalidAnnouncements << describe(iface);
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

// An app's widget that reports, FROM ITS OWN DESTRUCTOR, whether the
// accessibility cache still holds it. That is the decisive moment and the only
// one a test can stand in: a subclass destructor runs before ~QWidget, which is
// where Qt announces whatever the cache still has -- so "not cached here" is
// exactly "nothing to announce there".
class ProbeWidget : public QLabel
{
public:
    ProbeWidget(bool* cachedAtDestruction, QAccessible::Id* id)
        : QLabel(QStringLiteral("probe")), m_cached(cachedAtDestruction), m_id(id)
    {
        setObjectName(QStringLiteral("pluginWidget_probe"));
        setAttribute(Qt::WA_DontShowOnScreen);
    }

    ~ProbeWidget() override
    {
        *m_cached = QAccessible::accessibleInterface(*m_id) != nullptr;
    }

private:
    bool* m_cached;
    QAccessible::Id* m_id;
};

} // namespace

class DockCloseAccessibilityTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theAppsWidgetLeavesTheCacheBeforeItsDestructorRuns();
    void closingAnAppAnnouncesNoDestroyedWidget();
    void closingOneAppLeavesTheOtherReachable();

private:
    QAccessible::UpdateHandler m_previousHandler = nullptr;
};

void DockCloseAccessibilityTest::init()
{
    g_invalidAnnouncements.clear();
    // The announcing half of the crash: without it ~QWidget says nothing and
    // closingAnAppAnnouncesNoDestroyedWidget would pass for the wrong reason.
    // This only takes where the platform plugin provides a
    // QPlatformAccessibility, which is why that case checks isActive() first.
    QAccessible::setActive(true);
    m_previousHandler = QAccessible::installUpdateHandler(recordUpdate);
}

void DockCloseAccessibilityTest::cleanup()
{
    QAccessible::installUpdateHandler(m_previousHandler);
    QAccessible::setActive(false);
}

// THE MECHANISM, on every platform. No announcement is needed to see it: the
// widget asks about itself at the moment that decides whether there will be one.
void DockCloseAccessibilityTest::theAppsWidgetLeavesTheCacheBeforeItsDestructorRuns()
{
    bool cachedAtDestruction = true;
    QAccessible::Id id = 0;

    WorkspaceArea area;
    auto* probe = new ProbeWidget(&cachedAtDestruction, &id);
    area.addPluginDock(probe, QStringLiteral("my_app"), QStringLiteral("My App"));
    processDeferred();

    QDockWidget* dock = area.dockFor(QStringLiteral("my_app"));
    QVERIFY(dock);
    // As UIKit would: an interface for everything in the window, the probe
    // included. queryAccessibleInterface() caches whether or not accessibility
    // is switched on, which is what makes this case platform-independent.
    cacheInterfacesFor(&area);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(probe);
    QVERIFY(iface);
    id = QAccessible::uniqueId(iface);
    QVERIFY(id != 0);
    QVERIFY2(QAccessible::accessibleInterface(id) != nullptr,
             "the probe was not in the cache to begin with");

    QPointer<QDockWidget> alive(dock);
    area.removePluginDock(QStringLiteral("my_app"));
    processDeferred();
    QVERIFY2(alive.isNull(), "the dock was not destroyed, so nothing was proven");

    QVERIFY2(!cachedAtDestruction,
             "the app's widget was still in the accessibility cache when its destructor "
             "ran -- ~QWidget will announce it, and Qt's iOS plugin will ask the "
             "half-destroyed object for its role()");
}

void DockCloseAccessibilityTest::closingAnAppAnnouncesNoDestroyedWidget()
{
    if (!QAccessible::isActive()) {
        QSKIP("this platform plugin has no QPlatformAccessibility, so Qt announces "
              "nothing and the assertion would be vacuous (Qt 6.9 offscreen); the "
              "mechanism is covered by theAppsWidgetLeavesTheCacheBeforeItsDestructorRuns");
    }

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

    QVERIFY2(g_invalidAnnouncements.isEmpty(),
             qPrintable(QStringLiteral("closing an app announced %1 destroyed widget(s) "
                                       "whose accessible interface was already invalid: %2 "
                                       "-- Qt's iOS plugin calls role() on each of these")
                            .arg(g_invalidAnnouncements.size())
                            .arg(g_invalidAnnouncements.join(QStringLiteral(", ")))));
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
