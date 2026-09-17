// srcdeps: basecamp-shell/src/WebDriveFlows.cpp
//
// WHAT A `web` APP CAN BE DRIVEN THROUGH, and the verdict a page's controls
// cannot give (logos-workspace#238).
//
// The driver itself needs a device: a real pointer event crossing a container
// into a Qt-wasm page, and a module that answers it. What must not be wrong is
// smaller and belongs here -- that an app carries MORE THAN ONE named flow and
// a run can pick one (the whole shape of this issue), and the reading of the
// page console lines the Private-tab flow's verdicts are made of.
//
// THE READING IS THE PART THAT COULD LIE. "a `private sync running:` line
// appeared" is satisfied by the line the wallet publishes before it has asked
// for a single window, and "the cancel published something" is satisfied by a
// cancel that kept no block -- which is the resume contract broken and the one
// failure a human looking at the screen would not notice.
#include "basecamp-shell/src/WebDriveFlows.h"

#include <QtTest>

using basecamp::shell::WebDriveFlow;
using basecamp::shell::WebDriveFlows;
using basecamp::shell::WebDriveStep;
using basecamp::shell::WebPageWatch;
using basecamp::shell::WebPageWatcher;

namespace {

// The lines the wallet's `web` backend really prints -- `announce()` in
// src/wallet_ui_web_backend.cpp, as they arrive on the container's pageLog.
QString publication(const QString& state, const QString& body)
{
    return QStringLiteral("[wallet_ui web] private sync %1: %2").arg(state, body);
}

} // namespace

class WebDriveFlowsTest : public QObject
{
    Q_OBJECT

private slots:
    // THE ISSUE'S OWN SHAPE. One flow per app is what made the Private tab
    // uncheckable: reaching it meant replacing the seed import.
    void anAppCarriesSeveralNamedFlows()
    {
        const QStringList names = WebDriveFlows::namesFor(QStringLiteral("wallet_ui"));
        QVERIFY(names.size() >= 2);
        QVERIFY(names.contains(QStringLiteral("seed-import")));
        QVERIFY(names.contains(QStringLiteral("private-sync")));
    }

    // ...and a run picks one by name.
    void aFlowIsSelectedByName()
    {
        const WebDriveFlow priv =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("private-sync"));
        QCOMPARE(priv.name, QStringLiteral("private-sync"));
        QCOMPARE(priv.app, QStringLiteral("wallet_ui"));
        QVERIFY(!priv.isEmpty());

        const WebDriveFlow seed =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("seed-import"));
        QCOMPARE(seed.name, QStringLiteral("seed-import"));
        // Two flows, not one flow named twice.
        QVERIFY(seed.steps.first().control != priv.steps.first().control);
    }

    // NAMING NO FLOW IS THE COMPATIBILITY CONTRACT: `--drive web-input` drove
    // the seed import before this file existed and still does.
    void namingNoFlowGivesTheAppsFirst()
    {
        const WebDriveFlow first = WebDriveFlows::select(QStringLiteral("wallet_ui"), QString());
        QCOMPARE(first.name, QStringLiteral("seed-import"));
    }

    // An app with no flow, and a flow name no app has, are both "nothing to
    // do" -- the driver says which, and neither is a crash.
    void anUnknownAppOrFlowIsEmpty()
    {
        QVERIFY(WebDriveFlows::forApp(QStringLiteral("web_counter_b")).isEmpty());
        QVERIFY(WebDriveFlows::namesFor(QStringLiteral("web_counter_b")).isEmpty());
        QVERIFY(WebDriveFlows::select(QStringLiteral("web_counter_b"), QString()).isEmpty());
        QVERIFY(WebDriveFlows::select(QStringLiteral("wallet_ui"),
                                      QStringLiteral("kitchen-sink")).isEmpty());
    }

    // The Private flow is the issue's assertion list: press Private, ask for
    // the distance, start the walk, cancel it, require the block the cancel
    // kept and a percentage that MOVED.
    void thePrivateFlowPressesStartsCancelsAndAsserts()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("private-sync"));
        QStringList pressed;
        QStringList awaited;
        for (const WebDriveStep& step : flow.steps) {
            if (step.act == WebDriveStep::Act::Press
                || step.act == WebDriveStep::Act::PressAhead)
                pressed << step.control;
            if (step.act == WebDriveStep::Act::Await)
                awaited << step.watch.marker + step.watch.field;
        }
        QCOMPARE(pressed, (QStringList{ "Private", "Check", "Sync now", "Cancel" }));
        QCOMPARE(awaited.size(), 4);
        QVERIFY(awaited.at(1).contains(QLatin1String("private sync running:")));
        QVERIFY(awaited.at(2).contains(QLatin1String("private sync cancelled:")));
        QVERIFY(awaited.at(2).contains(QLatin1String("keptToBlock")));
        QVERIFY(awaited.at(3).contains(QLatin1String("percent")));
        QCOMPARE(pressed, (QStringList{ "Private", "Check", "Sync now", "Cancel" }));

        // ...and the percentage step is the MOVED one. A `Present` there would
        // pass on the line the wallet publishes before the first window.
        for (const WebDriveStep& step : flow.steps) {
            if (step.act != WebDriveStep::Act::Await) continue;
            if (step.watch.field == QLatin1String("percent"))
                QCOMPARE(step.watch.want, WebPageWatch::Want::Moved);
        }
    }

    // BOTH PRESSES GO IN BEFORE ANYTHING IS ASSERTED, and that order is what two
    // device runs measured. The whole cold Sepolia sync is 3.6 s on an M2
    // simulator, and worse, the host cannot act at all while it runs -- it
    // answers a `web` module's call synchronously on the thread this driver's
    // loop turns. So a Cancel sent on the strength of the `Sync now`
    // confirmation is 3.6 s late, and the view has disabled the button by then.
    void bothPressesAreArmedBeforeAnythingIsAsserted()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("private-sync"));
        int startAt = -1;
        int cancelAt = -1;
        int firstAwaitAfterStart = -1;
        int movedAt = -1;
        for (int i = 0; i < flow.steps.size(); ++i) {
            const WebDriveStep& step = flow.steps.at(i);
            if (step.control == QLatin1String("Sync now")) startAt = i;
            if (step.control == QLatin1String("Cancel")) cancelAt = i;
            if (step.act == WebDriveStep::Act::Await && startAt >= 0
                && firstAwaitAfterStart < 0)
                firstAwaitAfterStart = i;
            if (step.act == WebDriveStep::Act::Await
                && step.watch.want == WebPageWatch::Want::Moved)
                movedAt = i;
        }
        QVERIFY(startAt >= 0);
        // Neither press waits for the page to confirm it: the confirmation is
        // in a queue the host will not read until the walk is over.
        QCOMPARE(flow.steps.at(startAt).act, WebDriveStep::Act::PressAhead);
        QCOMPARE(flow.steps.at(cancelAt).act, WebDriveStep::Act::PressAhead);
        // The cancel is armed immediately after the start, and on the PAGE's
        // clock -- a delay measured on the host would be measured on a thread
        // that is about to stop.
        QCOMPARE(cancelAt, startAt + 1);
        QVERIFY(flow.steps.at(cancelAt).afterMs > 0);
        QVERIFY(firstAwaitAfterStart > cancelAt);
        // ...so the movement has to be read over the whole flow: by then every
        // line that carried it is behind the cursor.
        QVERIFY(movedAt > cancelAt);
        QVERIFY(flow.steps.at(movedAt).watch.overTheWholeFlow);
    }

    // Every Await has a budget, because a step with none waits forever on a
    // device nobody is watching.
    void everyAwaitIsBounded()
    {
        for (const QString& name : WebDriveFlows::namesFor(QStringLiteral("wallet_ui"))) {
            const WebDriveFlow flow = WebDriveFlows::select(QStringLiteral("wallet_ui"), name);
            for (const WebDriveStep& step : flow.steps) {
                if (step.act != WebDriveStep::Act::Await) continue;
                QVERIFY2(step.watch.budgetMs > 0, qPrintable(name));
                QVERIFY2(!step.watch.what.isEmpty(), qPrintable(name));
            }
        }
    }

    // ── the reading ────────────────────────────────────────────────────────

    void aFieldIsReadOffTheLineTheMarkerNames()
    {
        const QString line = publication(
            QStringLiteral("idle"),
            QStringLiteral(R"({"blocksRemaining":11721332,"percent":0,"targetBlock":11721332,)"
                           R"("state":"idle","leg":"sync","windows":0})"));
        QCOMPARE(WebPageWatcher::readingOf(line, QStringLiteral("private sync "),
                                           QStringLiteral("targetBlock")),
                 std::optional<QString>(QStringLiteral("11721332")));
        // ...and not off a line that is some other announcement.
        QCOMPARE(WebPageWatcher::readingOf(QStringLiteral("[wallet_ui web] balances: {\"x\":1}"),
                                           QStringLiteral("private sync "),
                                           QStringLiteral("targetBlock")),
                 std::nullopt);
    }

    // ABSENT AND NULL ARE THE SAME ANSWER. `etaMs` is null until a window has
    // completed; a watch that took it would report a number that is not there.
    void anAbsentOrNullFieldIsNotAReading()
    {
        const QString line = publication(QStringLiteral("running"),
                                         QStringLiteral(R"({"percent":0,"etaMs":null})"));
        QCOMPARE(WebPageWatcher::readingOf(line, QStringLiteral("private sync running:"),
                                           QStringLiteral("etaMs")),
                 std::nullopt);
        QCOMPARE(WebPageWatcher::readingOf(line, QStringLiteral("private sync running:"),
                                           QStringLiteral("keptToBlock")),
                 std::nullopt);
    }

    // THE HEART OF IT. `startPrivateSync` publishes `running` with the plan it
    // already had, BEFORE it asks for a window -- so a watch that settled on
    // the first line would report a walk that has not walked.
    void aMovedWatchIsNotSettledByTheFirstLine()
    {
        WebPageWatch watch;
        watch.marker = QStringLiteral("private sync running:");
        watch.field = QStringLiteral("percent");
        watch.want = WebPageWatch::Want::Moved;
        WebPageWatcher watcher(watch);

        QVERIFY(!watcher.offer(publication(QStringLiteral("running"),
                                           QStringLiteral(R"({"percent":0,"windows":0})"))));
        QVERIFY(!watcher.settled());
        QCOMPARE(watcher.baseline(), QStringLiteral("0"));
        // A second line saying the same thing is still not movement.
        QVERIFY(!watcher.offer(publication(QStringLiteral("running"),
                                           QStringLiteral(R"({"percent":0,"windows":0})"))));
        QVERIFY(!watcher.settled());
        QCOMPARE(watcher.linesSeen(), 2);

        QVERIFY(watcher.offer(publication(QStringLiteral("running"),
                                          QStringLiteral(R"({"percent":37,"windows":1})"))));
        QVERIFY(watcher.settled());
        QCOMPARE(watcher.reading(), QStringLiteral("37"));
    }

    // A watch reads ITS OWN lines. The wallet publishes `idle`, `running`,
    // `done` and `cancelled` through one announcement, so a marker that matched
    // the family would settle the running watch on the idle line that precedes
    // it.
    void aWatchIgnoresTheOtherStatesLines()
    {
        WebPageWatch watch;
        watch.marker = QStringLiteral("private sync running:");
        watch.field = QStringLiteral("percent");
        watch.want = WebPageWatch::Want::Moved;
        WebPageWatcher watcher(watch);

        QVERIFY(!watcher.offer(publication(QStringLiteral("idle"),
                                           QStringLiteral(R"({"percent":0})"))));
        QVERIFY(!watcher.offer(publication(QStringLiteral("done"),
                                           QStringLiteral(R"({"percent":100})"))));
        QCOMPARE(watcher.linesSeen(), 0);
    }

    // THE RESUME CONTRACT. A cancel that publishes `state: "cancelled"` and no
    // `keptToBlock` has thrown away the walk it just paid minutes for, and the
    // page looks exactly the same either way.
    void aCancelWithoutAKeptBlockDoesNotSettleTheWatch()
    {
        WebPageWatch watch;
        watch.marker = QStringLiteral("private sync cancelled:");
        watch.field = QStringLiteral("keptToBlock");
        watch.want = WebPageWatch::Want::Present;
        WebPageWatcher watcher(watch);

        QVERIFY(!watcher.offer(publication(
            QStringLiteral("cancelled"),
            QStringLiteral(R"({"cancelled":false,"state":"cancelled","leg":"sync"})"))));
        QVERIFY(!watcher.settled());
        QCOMPARE(watcher.linesSeen(), 1);   // it SAID it, and kept nothing

        QVERIFY(watcher.offer(publication(
            QStringLiteral("cancelled"),
            QStringLiteral(R"({"cancelled":true,"keptToBlock":11720700,"state":"cancelled"})"))));
        QCOMPARE(watcher.reading(), QStringLiteral("11720700"));
    }

    // WHAT THE FAST DEVICE ACTUALLY PUBLISHES. `running` at 0 %, and then --
    // because the cancel landed before the window did -- `cancelled` at 100 %
    // with the block it kept. The movement watch reads every state's line, so
    // that `cancelled:` line is what settles it.
    void theCancelledLineCanBeTheMovement()
    {
        WebPageWatch watch;
        watch.marker = QStringLiteral("private sync ");
        watch.field = QStringLiteral("percent");
        watch.want = WebPageWatch::Want::Moved;
        WebPageWatcher watcher(watch);

        // The wallet's first publication carries no plan at all.
        QVERIFY(!watcher.offer(publication(
            QStringLiteral("idle"),
            QStringLiteral(R"({"leg":"sync","note":"Not checked yet.","state":"idle"})"))));
        QVERIFY(!watcher.offer(publication(QStringLiteral("idle"),
                                           QStringLiteral(R"({"percent":0})"))));
        QVERIFY(!watcher.offer(publication(QStringLiteral("running"),
                                           QStringLiteral(R"({"percent":0})"))));
        QVERIFY(watcher.offer(publication(
            QStringLiteral("cancelled"),
            QStringLiteral(R"({"percent":100,"keptToBlock":11721544})"))));
        QCOMPARE(watcher.reading(), QStringLiteral("100"));
    }

    // A settled watch stays settled, so a driver may keep offering lines
    // without checking -- and the reading is the one that settled it.
    void aSettledWatchKeepsItsReading()
    {
        WebPageWatch watch;
        watch.marker = QStringLiteral("private sync cancelled:");
        watch.field = QStringLiteral("keptToBlock");
        WebPageWatcher watcher(watch);
        QVERIFY(watcher.offer(publication(QStringLiteral("cancelled"),
                                          QStringLiteral(R"({"keptToBlock":11720700})"))));
        QVERIFY(watcher.offer(publication(QStringLiteral("cancelled"),
                                          QStringLiteral(R"({"keptToBlock":99})"))));
        QCOMPARE(watcher.reading(), QStringLiteral("11720700"));
    }

    // A line that carries no object at all is not a reading, and is not a
    // crash: the page's console carries a 26 MB QML runtime's chatter too.
    void aLineWithNoObjectIsNotAReading()
    {
        QCOMPARE(WebPageWatcher::readingOf(QStringLiteral("private sync running: (nothing)"),
                                           QStringLiteral("private sync running:"),
                                           QStringLiteral("percent")),
                 std::nullopt);
    }
};

QTEST_MAIN(WebDriveFlowsTest)
#include "web_drive_flows_test.moc"
