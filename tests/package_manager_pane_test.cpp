// srcdeps: PackageManagerPane.cpp
//
// WHAT THE PACKAGE MANAGER SECTION SAYS WHEN package_manager_ui IS NOT THERE.
//
// The section is a page of MainContainer's content stack that starts as a
// placeholder and is REPLACED the moment the host hands over
// package_manager_ui's widget. Every build that ships the plugin does that
// within a few seconds, so the placeholder's text -- "Loading Package
// Manager…" -- was the only thing it ever needed to say.
//
// A Store shell does not ship it. A Bundled set is data (ADR 0007) and
// package_manager_ui is a desktop `ui_qml` plugin, so on a phone the mount is
// refused the moment it is asked for: BundledSetShellHost::mountApp reports
// "not a view module in this Bundled set" and returns. Nothing arrives, nothing
// fails, and the placeholder sat there claiming to be loading for the rest of
// the session (logos-workspace#145) -- on a build where package_manager itself
// was up and answering calls, which is what made the screen so hard to read.
//
// So the pane has three states rather than one, and the two that are not
// "loading" are the point:
//
//   * a load nobody asked for is IDLE, not loading;
//   * a load the host refused says so, in the host's own words;
//   * and a load that is neither refused nor delivered is declared dead on a
//     deadline -- because "the host silently dropped it" is a path this pane
//     cannot enumerate and must survive anyway.
//
// Run: nix build .#unit-tests -L
#include "PackageManagerPane.h"

#include <QtTest/QtTest>
#include <QApplication>

class PackageManagerPaneTest : public QObject {
    Q_OBJECT

private slots:
    // Nothing has asked for the plugin yet. The pane is built at startup, long
    // before the user opens the section, and a page that claims to be loading
    // something nobody requested is the defect in miniature.
    void aFreshPaneIsIdleAndDoesNotClaimToBeLoading()
    {
        PackageManagerPane pane;
        QCOMPARE(pane.state(), PackageManagerPane::Idle);
        QVERIFY(!pane.message().contains(QStringLiteral("Loading")));
        QVERIFY(!pane.message().isEmpty());
    }

    void beginLoadingSaysSo()
    {
        PackageManagerPane pane;
        pane.beginLoading();
        QCOMPARE(pane.state(), PackageManagerPane::Loading);
        QVERIFY(pane.message().contains(QStringLiteral("Loading Package Manager")));
    }

    // The host's words, verbatim: "not a view module in this Bundled set" is
    // the difference between a build that cannot show this screen and one whose
    // plugin crashed, and only the host knows which.
    void aRefusedLoadSaysWhyInTheHostsOwnWords()
    {
        PackageManagerPane pane;
        pane.beginLoading();
        pane.showUnavailable(QStringLiteral("app package_manager_ui is not a view "
                                            "module in this Bundled set"));
        QCOMPARE(pane.state(), PackageManagerPane::Unavailable);
        QVERIFY(pane.message().contains(QStringLiteral("not a view module")));
        QVERIFY(!pane.message().contains(QStringLiteral("Loading")));
    }

    // A host that reports nothing at all is the case this pane cannot detect by
    // asking -- MainUIBackend has load paths that neither deliver a widget nor
    // raise a failure (a load parked behind the dependency gate, a missing-deps
    // popup the user dismissed). The deadline is what makes those finite.
    void aLoadThatIsNeverAnsweredIsDeclaredDeadOnTheDeadline()
    {
        PackageManagerPane pane;
        pane.setLoadDeadline(1);
        pane.beginLoading();
        QTRY_COMPARE(pane.state(), PackageManagerPane::Unavailable);
        QVERIFY(!pane.message().contains(QStringLiteral("Loading")));
    }

    // The clock runs for a load, not for a pane. A pane put back after the
    // plugin was unloaded is idle, and idle is a resting state.
    void anIdlePaneNeverTimesOut()
    {
        PackageManagerPane pane;
        pane.setLoadDeadline(1);
        QTest::qWait(30);
        QCOMPARE(pane.state(), PackageManagerPane::Idle);
    }

    // The user leaves the section and comes back: the second attempt gets the
    // same full deadline as the first, and the previous failure is off screen.
    void askingAgainClearsTheFailureAndRestartsTheClock()
    {
        PackageManagerPane pane;
        pane.setLoadDeadline(1);
        pane.beginLoading();
        QTRY_COMPARE(pane.state(), PackageManagerPane::Unavailable);

        pane.setLoadDeadline(60000);
        pane.beginLoading();
        QCOMPARE(pane.state(), PackageManagerPane::Loading);
        QTest::qWait(30);
        QCOMPARE(pane.state(), PackageManagerPane::Loading);
    }

    // A host that refuses without saying why still owes the user a screen that
    // reads as an outcome.
    void aReasonlessRefusalStillReadsAsAnOutcome()
    {
        PackageManagerPane pane;
        pane.beginLoading();
        pane.showUnavailable(QString());
        QCOMPARE(pane.state(), PackageManagerPane::Unavailable);
        QVERIFY(pane.message().contains(QStringLiteral("Package Manager")));
        QVERIFY(!pane.message().contains(QStringLiteral("Loading")));
    }
};

QTEST_MAIN(PackageManagerPaneTest)
#include "package_manager_pane_test.moc"
