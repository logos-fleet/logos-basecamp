// srcdeps: webview/LiveRuntimeBudget.cpp
//
// The LIVE-RUNTIME BUDGET — the mobile Web container's answer to "how many
// Downloaded modules may have a QML runtime alive at once" (slice 28).
//
// The number it defends is not an abstraction. The Qt-wasm QML runtime measured
// 185–240 MB resident and 2.6–3 s of cold start in the spike, per page; a phone
// that kept one per installed module would be killed by the OS long before the
// user noticed. So the rule is: the runtime is alive only for the module the
// user is looking at, the others keep their Wasm host and drop their UI, and
// this class is where "the others" is decided.
//
// What is easy to regress and therefore pinned here:
//   * showing a second module must name the FIRST for eviction, not itself;
//   * re-showing a module that is already live must evict nothing (a repeated
//     tab click would otherwise tear the page down and rebuild it, which costs
//     the 3-second cold start every time);
//   * a module the container has already dropped must not be named again when
//     the next one is shown — a second teardown of a destroyed page is a crash,
//     not a no-op.
//
// Run: nix build .#unit-tests -L

#include "webview/LiveRuntimeBudget.h"

#include <QtTest/QtTest>

using basecamp::web::LiveRuntimeBudget;

class LiveRuntimeBudgetTest : public QObject {
    Q_OBJECT

private slots:
    void phoneKeepsOneRuntime()
    {
        LiveRuntimeBudget budget;
        QCOMPARE(budget.maxLiveRuntimes(), 1);
        QVERIFY(budget.live().isEmpty());
        QVERIFY(budget.visible().isEmpty());
    }

    void firstModuleEvictsNothing()
    {
        LiveRuntimeBudget budget;
        QCOMPARE(budget.show("counter_ui"), QStringList{});
        QCOMPARE(budget.live(), QStringList{"counter_ui"});
        QCOMPARE(budget.visible(), QString("counter_ui"));
        QVERIFY(budget.isLive("counter_ui"));
    }

    void switchingReleasesThePrevious()
    {
        LiveRuntimeBudget budget;
        budget.show("counter_ui");
        QCOMPARE(budget.show("notes_ui"), QStringList{"counter_ui"});
        QCOMPARE(budget.live(), QStringList{"notes_ui"});
        QVERIFY(!budget.isLive("counter_ui"));
    }

    void reshowingTheVisibleModuleEvictsNothing()
    {
        LiveRuntimeBudget budget;
        budget.show("counter_ui");
        QCOMPARE(budget.show("counter_ui"), QStringList{});
        QCOMPARE(budget.live(), QStringList{"counter_ui"});
    }

    void evictionIsLeastRecentlyVisibleFirst()
    {
        LiveRuntimeBudget budget(3);
        budget.show("a");
        budget.show("b");
        budget.show("c");
        // `a` is the oldest by visibility, and re-showing `a` here would make
        // `b` the oldest — which is the whole difference between LRU and a
        // load-order queue.
        budget.show("a");
        QCOMPARE(budget.show("d"), QStringList{"b"});
        QCOMPARE(budget.live(), (QStringList{"d", "a", "c"}));
    }

    void aDroppedModuleIsNotEvictedTwice()
    {
        LiveRuntimeBudget budget;
        budget.show("counter_ui");
        budget.forget("counter_ui");
        QVERIFY(!budget.isLive("counter_ui"));
        QCOMPARE(budget.show("notes_ui"), QStringList{});
    }

    void forgettingTheVisibleModuleLeavesNothingVisible()
    {
        LiveRuntimeBudget budget;
        budget.show("counter_ui");
        budget.forget("counter_ui");
        QVERIFY(budget.visible().isEmpty());
    }

    void theBudgetIsStatedInBytes()
    {
        LiveRuntimeBudget budget(2, 200ll * 1024 * 1024);
        QCOMPARE(budget.budgetBytes(), 400ll * 1024 * 1024);
        QCOMPARE(budget.projectedBytes(), 0ll);
        budget.show("a");
        QCOMPARE(budget.projectedBytes(), 200ll * 1024 * 1024);
        budget.show("b");
        QCOMPARE(budget.projectedBytes(), 400ll * 1024 * 1024);
        budget.show("c");
        QCOMPARE(budget.projectedBytes(), 400ll * 1024 * 1024);
    }

    void aBudgetOfZeroStillKeepsTheVisibleModule()
    {
        // Nothing can be shown without a runtime, so "no runtimes" is not a
        // configuration the container can honour — it would evict the page it
        // is being asked to show. One is the floor.
        LiveRuntimeBudget budget(0);
        QCOMPARE(budget.maxLiveRuntimes(), 1);
        QCOMPARE(budget.show("counter_ui"), QStringList{});
        QVERIFY(budget.isLive("counter_ui"));
    }
};

QTEST_MAIN(LiveRuntimeBudgetTest)
#include "live_runtime_budget_test.moc"
