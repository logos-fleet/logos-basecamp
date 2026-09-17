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
        QVERIFY(names.size() >= 5);
        QVERIFY(names.contains(QStringLiteral("seed-import")));
        QVERIFY(names.contains(QStringLiteral("private-sync")));
        QVERIFY(names.contains(QStringLiteral("private-shield")));
        // ...and the two screens #250 was reported against, which no flow
        // reached: the operator's History and Proxy config failures were read
        // off a photograph and nothing in this repo could ask the tabs again.
        QVERIFY(names.contains(QStringLiteral("history")));
        QVERIFY(names.contains(QStringLiteral("proxy-config")));
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

    // ── the shield, which is the same shape and a different safety argument ──
    //
    // logos-workspace#235's other direction: `wrap` / `approve` / `shield`, the
    // legs that put PUBLIC funds into the pool. The flow drives them on a
    // device with no money in it and cannot reach a transaction, BY
    // CONSTRUCTION rather than by being careful: the route parks at `sign`
    // waiting for a human in a Signer app that is not installed here, and the
    // cancel is pressed there. Nothing is ever signed or broadcast.
    void theShieldFlowPlansAndThenLeavesBeforeAnythingIsSigned()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("private-shield"));
        QCOMPARE(flow.name, QStringLiteral("private-shield"));
        QVERIFY(!flow.isEmpty());

        QStringList pressed;
        QStringList typedInto;
        QStringList awaited;
        for (const WebDriveStep& step : flow.steps) {
            if (step.act == WebDriveStep::Act::Press
                || step.act == WebDriveStep::Act::PressAhead)
                pressed << step.control;
            if (step.act == WebDriveStep::Act::Type)
                typedInto << step.control;
            if (step.act == WebDriveStep::Act::Await)
                awaited << step.watch.marker + step.watch.field;
        }

        // THE BUTTONS ARE PRESSED BY objectName. The accessible name is matched
        // case-insensitively FROM THE FRONT, and this tab has a "Shield" button
        // under a "Shield into the pool" heading and three controls whose names
        // begin with "Cancel" — so a flow that named them by text would be one
        // layout change away from pressing the wrong one.
        QCOMPARE(pressed, (QStringList{ "Private", "privateShieldButton",
                                        "privateShieldCancelButton" }));
        QCOMPARE(typedInto, (QStringList{ "privateShieldAssetField",
                                          "privateShieldAmountField" }));

        QCOMPARE(awaited.size(), 3);
        QVERIFY(awaited.at(0).contains(QLatin1String("private shield running:")));
        QVERIFY(awaited.at(0).contains(QLatin1String("leg")));
        // THE CANCEL LEFT SOMETHING TO SAY, which is clause 2's answer: a note
        // stating that nothing was signed and nothing reached the chain.
        QVERIFY(awaited.at(1).contains(QLatin1String("private shield cancelled:")));
        QVERIFY(awaited.at(1).contains(QLatin1String("note")));
        QVERIFY(awaited.at(2).contains(QLatin1String("state")));
    }

    // ...and the same arming rule, for the same reason: the plan's RPC reads
    // are answered synchronously on the thread this driver's loop turns, so the
    // host cannot read "pressed 'Shield'" until they are done. A Cancel sent on
    // the strength of that confirmation would be seconds late.
    void theShieldsTwoPressesAreArmedTogether()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("private-shield"));
        int startAt = -1;
        int cancelAt = -1;
        int firstAwait = -1;
        int movedAt = -1;
        for (int i = 0; i < flow.steps.size(); ++i) {
            const WebDriveStep& step = flow.steps.at(i);
            if (step.control == QLatin1String("privateShieldButton")) startAt = i;
            if (step.control == QLatin1String("privateShieldCancelButton")) cancelAt = i;
            if (step.act == WebDriveStep::Act::Await && firstAwait < 0) firstAwait = i;
            if (step.act == WebDriveStep::Act::Await
                && step.watch.want == WebPageWatch::Want::Moved)
                movedAt = i;
        }
        QVERIFY(startAt >= 0);
        QCOMPARE(flow.steps.at(startAt).act, WebDriveStep::Act::PressAhead);
        QCOMPARE(flow.steps.at(cancelAt).act, WebDriveStep::Act::PressAhead);
        QCOMPARE(cancelAt, startAt + 1);
        QVERIFY(flow.steps.at(cancelAt).afterMs > 0);
        QVERIFY(firstAwait > cancelAt);
        // The state that moved is read over the whole flow: on a fast device
        // every line is published inside the seconds the plan takes.
        QVERIFY(movedAt > cancelAt);
        QVERIFY(flow.steps.at(movedAt).watch.overTheWholeFlow);
    }

    // ── #250's two other screens ───────────────────────────────────────────
    //
    // THE HISTORY TAB, AND THE ACCOUNT IN FRONT OF IT. `Refresh history` is
    // disabled until the wallet holds an account (`enabled: root.ready &&
    // acctBox.currentText.length > 0`), so a flow that pressed it on a freshly
    // installed app would press a dead button and then time out waiting for an
    // answer nobody was asked for. The flow therefore imports the same
    // worthless all-zero BIP-39 vector the seed flow types, WAITS for the
    // wallet to publish an account, and only then asks the tab.
    //
    // The verdict is `history updated:`, which the wallet publishes only when
    // the coordinator ANSWERED -- a refusal publishes no such line. `rows` is
    // the reading and not its emptiness: an account with no transactions is an
    // answer, and it is the answer this device will get.
    void theHistoryFlowMakesAnAccountAndThenAsksTheCoordinator()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("history"));
        QCOMPARE(flow.name, QStringLiteral("history"));
        QCOMPARE(flow.app, QStringLiteral("wallet_ui"));
        QVERIFY(!flow.isEmpty());

        QStringList pressed;
        QStringList typedInto;
        QStringList awaited;
        int accountAwaitAt = -1;
        int refreshPressAt = -1;
        for (int i = 0; i < flow.steps.size(); ++i) {
            const WebDriveStep& step = flow.steps.at(i);
            if (step.act == WebDriveStep::Act::Press
                || step.act == WebDriveStep::Act::PressAhead) {
                pressed << step.control;
                if (step.control == QLatin1String("historyRefreshButton")) refreshPressAt = i;
            }
            if (step.act == WebDriveStep::Act::Type)
                typedInto << step.control;
            if (step.act == WebDriveStep::Act::Await) {
                awaited << step.watch.marker + step.watch.field;
                if (step.watch.marker.contains(QLatin1String("accounts now:")))
                    accountAwaitAt = i;
            }
        }

        // THE TABS BY THEIR TEXT AND THE BUTTONS BY objectName. An accessible
        // name is matched case-insensitively FROM THE FRONT, and the Advanced
        // tab's "Import" button sits under an "Import account (seed phrase)"
        // heading that matches "Import" just as well -- a press by text there
        // is one layout read away from pressing a label.
        QCOMPARE(pressed, (QStringList{ "Advanced", "advImportButton", "History",
                                        "historyRefreshButton" }));
        QCOMPARE(typedInto, (QStringList{ "advSeedField", "advAcctLabelField",
                                          "advAcctPwField" }));
        // THE GATE IS BEFORE THE PRESS, not after it.
        QVERIFY(accountAwaitAt >= 0);
        QVERIFY(refreshPressAt > accountAwaitAt);

        QCOMPARE(awaited.size(), 2);
        QVERIFY(awaited.at(0).contains(QLatin1String("accounts now:")));
        QVERIFY(awaited.at(0).contains(QLatin1String("selected")));
        QVERIFY(awaited.at(1).contains(QLatin1String("history updated:")));
        QVERIFY(awaited.at(1).contains(QLatin1String("rows")));
    }

    // THE SETTINGS TAB'S PROXY CONFIG, which the operator saw refuse with
    // "Proxy config needs wallet_backend_module". It is two claims: the
    // coordinator took the document (the console line, carrying what was
    // applied), and the TAB SAYS SO -- `proxyStatus` was a property nothing
    // rendered, so pressing Apply changed the screen in no way whatever
    // happened. The readback is of the module's own state through a control,
    // which is the strongest verdict this driver has.
    void theProxyFlowAppliesADocumentAndTheTabSaysSo()
    {
        const WebDriveFlow flow =
            WebDriveFlows::select(QStringLiteral("wallet_ui"), QStringLiteral("proxy-config"));
        QCOMPARE(flow.name, QStringLiteral("proxy-config"));
        QVERIFY(!flow.isEmpty());

        QStringList pressed;
        QStringList typedInto;
        QStringList awaited;
        QString readControl;
        QString readHolds;
        for (const WebDriveStep& step : flow.steps) {
            if (step.act == WebDriveStep::Act::Press
                || step.act == WebDriveStep::Act::PressAhead)
                pressed << step.control;
            if (step.act == WebDriveStep::Act::Type)
                typedInto << step.control;
            if (step.act == WebDriveStep::Act::Await)
                awaited << step.watch.marker + step.watch.field;
            if (step.act == WebDriveStep::Act::Read) {
                readControl = step.control;
                readHolds = step.text;
            }
        }

        QCOMPARE(pressed, (QStringList{ "Settings", "proxyApplyButton" }));
        QCOMPARE(typedInto, (QStringList{ "proxyUrlField" }));
        QCOMPARE(awaited.size(), 1);
        QVERIFY(awaited.at(0).contains(QLatin1String("proxy applied:")));
        QVERIFY(awaited.at(0).contains(QLatin1String("proxy")));
        // The tab's own line, and it must carry the proxy that was typed --
        // "applied" with no address in it is a sentence about nothing.
        QCOMPARE(readControl, QStringLiteral("proxyStatusText"));
        QVERIFY(!readHolds.isEmpty());
        for (const WebDriveStep& step : flow.steps)
            if (step.act == WebDriveStep::Act::Type)
                QVERIFY(readHolds.contains(step.text));
    }

    // A HISTORY WITH NOTHING IN IT IS STILL AN ANSWER. `rows: 0` is the reading
    // a fresh account on a device gives, and a watcher that treated 0 as
    // "nothing there" would time out on the very run this issue is about.
    void aHistoryWithNoRowsIsStillAReading()
    {
        const QString line = QStringLiteral(
            R"([wallet_ui web] history updated: {"address":"0x1","from":"wallet_backend_module",)"
            R"("rows":0})");
        QCOMPARE(WebPageWatcher::readingOf(line, QStringLiteral("history updated:"),
                                           QStringLiteral("rows")),
                 std::optional<QString>(QStringLiteral("0")));
        // ...and the ASK is not the answer: the wallet announces it before it
        // knows anything, and a watch that matched it would settle on a
        // refusal.
        QCOMPARE(WebPageWatcher::readingOf(
                     QStringLiteral("[wallet_ui web] refreshHistory: asking "
                                    "wallet_backend_module for 0x1, channel=yes admitted=yes"),
                     QStringLiteral("history updated:"), QStringLiteral("rows")),
                 std::nullopt);
    }

    // ...AND AN EMPTY KEYSTORE IS NOT AN ACCOUNT. The wallet publishes this
    // line once at startup for a list with nothing in it, before the flow has
    // imported anything, and it lands after the flow's cursor. `selected` is
    // null there rather than an empty string precisely so this gate does NOT
    // open on it and go on to press a button the view has disabled.
    void anEmptyAccountListDoesNotOpenTheHistoryGate()
    {
        QCOMPARE(WebPageWatcher::readingOf(
                     QStringLiteral(R"([wallet_ui web] accounts now: {"count":0,)"
                                    R"("selected":null,"from":"keystore_module"})"),
                     QStringLiteral("accounts now:"), QStringLiteral("selected")),
                 std::nullopt);
        QCOMPARE(WebPageWatcher::readingOf(
                     QStringLiteral(R"([wallet_ui web] accounts now: {"count":1,)"
                                    R"("selected":"0xabc","from":"keystore_module"})"),
                     QStringLiteral("accounts now:"), QStringLiteral("selected")),
                 std::optional<QString>(QStringLiteral("0xabc")));
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
