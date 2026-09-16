// srcdeps: AppNotices.cpp
//
// WHAT THE SHELL PUTS ON SCREEN WHEN AN APP CANNOT COME UP (logos-workspace#205).
//
// The host has exactly one negative edge -- IShellObserver::onUiModuleUnavailable
// -- and until now the Shell acted on exactly one name through it:
// package_manager_ui, the module it hoists into a page of its own. Every other
// refusal reached a log. So a `ui_qml` app whose declared module this device
// does not have, and a `web` app whose page never opened, were both a tile
// press that did nothing at all: no window, no message, no way to tell a
// refusal from a slow load.
//
// THE RULE IS HERE RATHER THAN IN MainContainer because MainContainer is a
// QQuickWidget, a sidebar, an overlay and a dock area, none of which a unit
// test can stand up -- and the part that was wrong is none of those. It is
// which refusals deserve a screen, what happens to that screen when the app
// later comes up, and what closing it means. `raise`/`drop` are supplied by
// the caller for the same reason bringUpViewDependencies takes a `load`
// (ViewDependencies.h).
//
// Run: nix build .#unit-tests -L

#include "AppNotices.h"

#include <QtTest/QtTest>

using basecamp::shell::AppNotices;

namespace {

// Records what the Shell was asked to put on screen and take off it.
struct Screen {
    QStringList raised;    // "name: reason" in the order they were asked for
    QStringList dropped;

    AppNotices make()
    {
        return AppNotices([this](const QString& name, const QString& reason) {
                              raised << name + QStringLiteral(": ") + reason;
                          },
                          [this](const QString& name) { dropped << name; });
    }
};

const QLatin1String kChatUi("chat_ui");
// The host's own words, verbatim from BundledSetShellHost::mountApp.
const QLatin1String kWhy("app chat_ui needs modules it cannot have: "
                         "this device does not have delivery_module");

} // namespace

class AppNoticesTest : public QObject
{
    Q_OBJECT

private slots:
    // THE DEFECT. A refused mount with nothing of that app on screen has to
    // become a screen, carrying the host's reason.
    void aRefusalWithNothingOnScreenBecomesOne();
    // The same app refused twice is ONE screen with the later words on it --
    // not two tabs for one app, which is what a second raise would be.
    void asecondRefusalRefreshesTheSameScreen();
    // A LATE refusal must not take a working app away. The host can refuse a
    // re-load of a module whose widget is already docked, and replacing that
    // with an error message is the worse bug.
    void aRefusalIsIgnoredWhileTheAppIsOnScreen();
    // The app comes up after all -- the user loaded the module by hand from
    // the Modules tab, which is exactly how #205 was diagnosed. The notice
    // goes before the widget arrives, or the app has two tabs.
    void theNoticeGoesWhenTheRealAppArrives();
    // ...and an arrival nobody warned about is not an error.
    void anArrivalWithNoNoticeIsQuiet();
    // Closing a notice's tab is not an unload: there is nothing loaded behind
    // it, and telling the host otherwise reports "app is not mounted" at a
    // user who just closed a message.
    void closingANoticeIsNotAnUnload();
    // Closing an ordinary app's tab still reaches the host.
    void closingAnAppIsStillAnUnload();
    // An empty reason still produces a screen. A host that refuses without
    // words is worse than one that does, and a silent tile press is the thing
    // being fixed.
    void anEmptyReasonStillProducesAScreen();
};

void AppNoticesTest::aRefusalWithNothingOnScreenBecomesOne()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, kWhy, /*mounted=*/false);

    QCOMPARE(screen.raised, QStringList({ QString(kChatUi) + QStringLiteral(": ") + kWhy }));
    QVERIFY(notices.holds(kChatUi));
    QCOMPARE(notices.reasonFor(kChatUi), QString(kWhy));
}

void AppNoticesTest::asecondRefusalRefreshesTheSameScreen()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, kWhy, false);
    notices.unavailable(kChatUi, QStringLiteral("and now for another reason"), false);

    QCOMPARE(screen.raised.size(), 2);
    QVERIFY(screen.raised.last().endsWith(QStringLiteral("and now for another reason")));
    // One screen, refreshed -- never taken away and put back.
    QVERIFY(screen.dropped.isEmpty());
    QCOMPARE(notices.names(), QStringList({ QString(kChatUi) }));
}

void AppNoticesTest::aRefusalIsIgnoredWhileTheAppIsOnScreen()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, kWhy, /*mounted=*/true);

    QVERIFY(screen.raised.isEmpty());
    QVERIFY(!notices.holds(kChatUi));
}

void AppNoticesTest::theNoticeGoesWhenTheRealAppArrives()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, kWhy, false);
    notices.arrived(kChatUi);

    QCOMPARE(screen.dropped, QStringList({ QString(kChatUi) }));
    QVERIFY(!notices.holds(kChatUi));
    QVERIFY(notices.names().isEmpty());
}

void AppNoticesTest::anArrivalWithNoNoticeIsQuiet()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.arrived(kChatUi);
    QVERIFY(screen.dropped.isEmpty());
}

void AppNoticesTest::closingANoticeIsNotAnUnload()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, kWhy, false);

    QVERIFY(notices.closed(kChatUi));
    QCOMPARE(screen.dropped, QStringList({ QString(kChatUi) }));
    QVERIFY(!notices.holds(kChatUi));
}

void AppNoticesTest::closingAnAppIsStillAnUnload()
{
    Screen screen;
    AppNotices notices = screen.make();
    QVERIFY(!notices.closed(kChatUi));
    QVERIFY(screen.dropped.isEmpty());
}

void AppNoticesTest::anEmptyReasonStillProducesAScreen()
{
    Screen screen;
    AppNotices notices = screen.make();
    notices.unavailable(kChatUi, QString(), false);

    QCOMPARE(screen.raised.size(), 1);
    QVERIFY(notices.holds(kChatUi));
}

QTEST_MAIN(AppNoticesTest)
#include "app_notices_test.moc"
