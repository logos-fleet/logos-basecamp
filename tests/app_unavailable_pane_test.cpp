// srcdeps: AppUnavailablePane.cpp
//
// THE SCREEN ITSELF (logos-workspace#205).
//
// AppNotices decides WHEN one of these belongs on screen; this is what is on
// it. Two things are load-bearing and both were the defect:
//
//   * the app's name, because the tab the user pressed is the only context
//     they have, and
//   * the host's own words, because "this device does not have
//     delivery_module" and "the image would not load" are different
//     instructions and only the host can tell them apart (IShellHost.h).
//
// Qt widgets and nothing else, like PackageManagerPane beside it: this
// compiles inside main_ui, which links Qt alone.
//
// Run: nix build .#unit-tests -L

#include "AppUnavailablePane.h"

#include <QtTest/QtTest>
#include <QApplication>

class AppUnavailablePaneTest : public QObject
{
    Q_OBJECT

private slots:
    void namesTheAppAndSaysItCannotOpen()
    {
        AppUnavailablePane pane(QStringLiteral("Chat"), QStringLiteral("no delivery_module"));
        QVERIFY(pane.message().contains(QStringLiteral("Chat")));
        QVERIFY(pane.message().contains(QStringLiteral("no delivery_module")));
    }

    // The host's words verbatim, not a summary of them.
    void carriesTheHostsReasonUnedited()
    {
        const QString why = QStringLiteral("app chat_ui needs modules it cannot have: "
                                           "this device does not have delivery_module");
        AppUnavailablePane pane(QStringLiteral("Chat"), why);
        QVERIFY(pane.message().contains(why));
    }

    // A refusal with no words is still an outcome, not a blank page: the
    // silent tile press is what this class exists to end.
    void anEmptyReasonStillReadsAsAnOutcome()
    {
        AppUnavailablePane pane(QStringLiteral("Chat"), QString());
        QVERIFY(!pane.message().trimmed().isEmpty());
        QVERIFY(pane.message().contains(QStringLiteral("Chat")));
    }

    // A later refusal replaces the words without rebuilding the page -- the
    // tab stays where it is (AppNotices refreshes rather than re-raising).
    void aRefreshReplacesTheReason()
    {
        AppUnavailablePane pane(QStringLiteral("Chat"), QStringLiteral("first"));
        pane.setReason(QStringLiteral("second"));
        QVERIFY(pane.message().contains(QStringLiteral("second")));
        QVERIFY(!pane.message().contains(QStringLiteral("first")));
    }

    // The one handle a driver on a phone has on this page: it is a widget, not
    // a QML item, so there is no id to find it by.
    void carriesAStableHandleForADriver()
    {
        AppUnavailablePane pane(QStringLiteral("Chat"), QStringLiteral("why"));
        QVERIFY(pane.findChild<QObject*>(QStringLiteral("appUnavailablePane.message")));
    }
};

QTEST_MAIN(AppUnavailablePaneTest)
#include "app_unavailable_pane_test.moc"
