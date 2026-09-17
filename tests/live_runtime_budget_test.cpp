// srcdeps: webview/LiveRuntimeBudget.cpp webview/AppMemory.cpp
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

#include "webview/AppMemory.h"
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
    // ── #153: THE DEVICE ANSWERS, AND THE MEASUREMENT IS READ ──────────────
    //
    // The count used to be 1 on every device and the memory the container
    // measures was never consulted by anything but a log line. Both halves are
    // pinned here: what a device of a given size is allowed to keep, and what
    // happens when the app grows past what that device can afford.

    void theCountComesFromTheDevicesMemory()
    {
        // A 6 GB phone affords more than one page; a 2 GB one does not, and a
        // desktop-class machine is not allowed to keep everything.
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(6LL * 1024 * 1024 * 1024), 3);
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(4LL * 1024 * 1024 * 1024), 2);
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(2LL * 1024 * 1024 * 1024), 1);
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(64LL * 1024 * 1024 * 1024), 3);
    }

    void aDeviceThatWillNotSayGetsThePhonesAnswer()
    {
        // appResidentBytes()/deviceMemoryBytes() answer -1 where the platform
        // will not say, and a budget computed from -1 must be the conservative
        // one rather than nothing at all.
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(-1), 1);
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(0), 1);
    }

    void aFatterPageBuysFewerOfThem()
    {
        // The count is the device's memory divided by what one page costs, so a
        // page measured at twice the size halves what the same device keeps.
        QCOMPARE(LiveRuntimeBudget::runtimesForDeviceMemory(4LL * 1024 * 1024 * 1024,
                                                           580LL * 1024 * 1024), 1);
    }

    void theAppIsGivenACeilingToWeighAgainst()
    {
        // A THIRD of the device is what the app as a WHOLE may weigh -- not the
        // pages' share, which is the smaller number the count comes from, and
        // deliberately under the figure the OS acts on rather than at it.
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceMemory(3LL * 1024 * 1024 * 1024),
                 1LL * 1024 * 1024 * 1024);
        // ...and a device that will not say gives no ceiling rather than a
        // ceiling of zero, which would evict on every observation.
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceMemory(-1), Q_INT64_C(0));
    }

    void growingPastTheCeilingGivesUpTheOldestPage()
    {
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        budget.show("c");
        QCOMPARE(budget.live().size(), 3);

        // Inside the ceiling: nothing to do. The books are not touched by a
        // measurement that says the app is fine.
        QCOMPARE(budget.observe(500LL * 1024 * 1024), QStringList{});
        QCOMPARE(budget.live().size(), 3);

        // Over it: one page, the least recently visible, and not the one the
        // user is looking at.
        QCOMPARE(budget.observe(1200LL * 1024 * 1024), QStringList{"a"});
        QCOMPARE(budget.live(), (QStringList{"c", "b"}));
        QCOMPARE(budget.liveAllowance(), 2);

        // Still over it after the first page went: the next one goes too.
        QCOMPARE(budget.observe(1200LL * 1024 * 1024), QStringList{"b"});
        QCOMPARE(budget.live(), QStringList{"c"});
    }

    void shrinkingNeverTakesTheVisibleModule()
    {
        // One page is the floor whatever the measurement says: a container that
        // evicted the page the user is looking at would leave them staring at
        // nothing and would still not have fixed the memory, because the next
        // show() would load it again.
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        QCOMPARE(budget.observe(9000LL * 1024 * 1024), QStringList{});
        QCOMPARE(budget.live(), QStringList{"a"});
        QCOMPARE(budget.liveAllowance(), 1);
    }

    void aPlatformThatWillNotSayIsNotAReasonToEvict()
    {
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        QCOMPARE(budget.observe(-1), QStringList{});
        QCOMPARE(budget.live().size(), 2);
        QCOMPARE(budget.liveAllowance(), 3);
    }

    void withNoCeilingNothingIsObserved()
    {
        // A budget built without a ceiling (no device reading, or a host that
        // states its own count) counts and does not weigh -- which is exactly
        // what it did before this issue, and is still the answer where there is
        // nothing to weigh against.
        LiveRuntimeBudget budget(2, 100LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        QCOMPARE(budget.observe(9000LL * 1024 * 1024), QStringList{});
        QCOMPARE(budget.live().size(), 2);
    }

    void theBudgetComesBackWhenTheAppIsSmallAgain()
    {
        // HYSTERESIS, and it is the reason for the two thresholds. A budget that
        // relaxed the moment it dipped under the ceiling would tear the page
        // down and build it again around a number that oscillates by a few MB;
        // it takes a clear margin below the ceiling to be allowed to grow.
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        budget.show("c");
        budget.observe(1200LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 2);

        // Just under the ceiling is not "small again".
        budget.observe(950LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 2);

        // Well under it is.
        budget.observe(600LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 3);
    }

    void aMemoryWarningKeepsOnlyWhatTheUserIsLookingAt()
    {
        // The OS asked, which is the one signal that is not a guess -- iOS will
        // kill the app rather than ask twice -- so this is not one page at a
        // time.
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        budget.show("c");
        QCOMPARE(budget.shedUnderPressure(), (QStringList{"a", "b"}));
        QCOMPARE(budget.live(), QStringList{"c"});
        QCOMPARE(budget.visible(), QString("c"));
        QCOMPARE(budget.liveAllowance(), 1);
    }

    void aWarningWithNothingToShedIsANoOp()
    {
        LiveRuntimeBudget budget;
        QCOMPARE(budget.shedUnderPressure(), QStringList{});
        budget.show("a");
        QCOMPARE(budget.shedUnderPressure(), QStringList{});
        QVERIFY(budget.isLive("a"));
    }

    void aTightenedBudgetStillHoldsTheNextModuleShown()
    {
        // After a warning the allowance is one, and show() has to honour THAT
        // rather than the device's number -- otherwise the app grows straight
        // back to the size the OS just complained about.
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        budget.shedUnderPressure();
        QCOMPARE(budget.show("c"), QStringList{"b"});
        QCOMPARE(budget.live(), QStringList{"c"});
    }

    void aRunCanStateTheBudgetOnTheCommandLine()
    {
        // The phone's spelling: an APK's process inherits no environment a
        // developer typed, and both launchers forward arguments.
        const LiveRuntimeBudget budget = LiveRuntimeBudget::forThisDevice(
            QStringList{ "basecamp-shell", "--web-budget", "3", "--drive", "web-budget" });
        QCOMPARE(budget.maxLiveRuntimes(), 3);

        // A flag with nothing usable after it is not a budget of zero: the
        // policy answers, exactly as it does when nobody said anything.
        const LiveRuntimeBudget ignored =
            LiveRuntimeBudget::forThisDevice(QStringList{ "basecamp-shell", "--web-budget" });
        QCOMPARE(ignored.maxLiveRuntimes(),
                 LiveRuntimeBudget::runtimesForDeviceMemory(basecamp::web::deviceMemoryBytes()));
    }

    void aHostCanStateTheBudgetItself()
    {
        // The measurement is the default, not a law: a host that knows better
        // (a test, a device run pinning a number) says so, and nothing reads the
        // device behind its back.
        qputenv("LOGOS_WEB_RUNTIME_BUDGET", "2");
        const LiveRuntimeBudget budget = LiveRuntimeBudget::forThisDevice();
        qunsetenv("LOGOS_WEB_RUNTIME_BUDGET");
        QCOMPARE(budget.maxLiveRuntimes(), 2);
    }

    void theDeviceSaysHowMuchMemoryItHas()
    {
        // Whatever this test runs on is a real machine with real memory; the
        // point is that the number exists and is sane, exactly as
        // appResidentBytes()'s is.
        const qint64 bytes = basecamp::web::deviceMemoryBytes();
        QVERIFY2(bytes > 0, qPrintable(QStringLiteral("deviceMemoryBytes() = %1").arg(bytes)));
        QVERIFY(bytes > 256LL * 1024 * 1024);
        QVERIFY(bytes < Q_INT64_C(1024) * 1024 * 1024 * 1024);
    }

    void theDeviceAlsoSaysHowMuchOfItIsLeft()
    {
        // #230: THE ONLY NUMBER ON ANDROID A `web` PAGE APPEARS IN.
        //
        // appResidentBytes() is this process and a page is not in this process.
        // Measured on a Xiaomi 25028RN03Y (2.7 GB) on 2026-09-17: the first
        // `web` page moved the app's own figure by 101 MB and the second by
        // 1 MB, while the Chromium renderer holding both went from nothing to
        // 255 MB -- and MemAvailable moved by the renderer's amount. A pass
        // that reported only the app's figure under-reported the page by 2.5x.
        const qint64 available = basecamp::web::deviceAvailableBytes();
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        // Where /proc/meminfo exists the reading is real, and it is a SHARE of
        // the device rather than a figure of its own -- a machine cannot have
        // more memory free than it has.
        QVERIFY2(available > 0,
                 qPrintable(QStringLiteral("deviceAvailableBytes() = %1").arg(available)));
        QVERIFY(available <= basecamp::web::deviceMemoryBytes());
#else
        // ...and where it does not, the answer is "there is no such reading"
        // rather than a number derived from one that means something else. iOS
        // charges jetsam a per-process footprint and publishes no device-wide
        // free figure to an app.
        QCOMPARE(available, Q_INT64_C(-1));
#endif
    }
};

QTEST_MAIN(LiveRuntimeBudgetTest)
#include "live_runtime_budget_test.moc"
