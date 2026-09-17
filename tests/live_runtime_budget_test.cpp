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
        // The stated cost of N pages is ONE renderer and a document each after
        // it -- see theStatedCostIsOneRendererAndThenAlmostNothing(). What this
        // pins is that the held figure tracks the live set and stops at the
        // budget: a third module shown against a budget of two evicts one, so
        // what is held does not grow.
        const qint64 extra = LiveRuntimeBudget::kAdditionalRuntimeBytes;
        LiveRuntimeBudget budget(2, 200ll * 1024 * 1024);
        QCOMPARE(budget.budgetBytes(), 200ll * 1024 * 1024 + extra);
        QCOMPARE(budget.projectedBytes(), 0ll);
        budget.show("a");
        QCOMPARE(budget.projectedBytes(), 200ll * 1024 * 1024);
        budget.show("b");
        QCOMPARE(budget.projectedBytes(), 200ll * 1024 * 1024 + extra);
        budget.show("c");
        QCOMPARE(budget.projectedBytes(), 200ll * 1024 * 1024 + extra);
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

    // ── #244: THE FIGURE THE BUDGET READS HAS TO MOVE WITH THE PAGES ───────
    //
    // #153 gave the budget a ceiling and observe() weighed appResidentBytes()
    // against it. On Android that reading is BLIND to the thing the budget
    // governs -- every `web` page of a build lives in one Chromium renderer
    // which is a separate process -- so the branch was unreachable and the
    // policy silently degenerated to the fixed count it replaced. Measured on
    // 2026-09-17 (logos-workspace#244):
    //
    //   Xiaomi 25028RN03Y  app 303/435/434/435 MB at 0/1/2/3 live runtimes
    //   Samsung SM-G990B   app 254/293/268/271 MB at 0/1/2/3 live runtimes
    //
    // ...and on the eviction at the end of the pass the app's figure went UP
    // 3 MB while the renderer holding the pages fell 65 MB. Wrong sign, not
    // merely insensitive. So what observe() weighs is now a PER-PLATFORM
    // choice, and so is the frame its ceiling is stated in.

    void theWeighedFigureIsThePlatformsOwn()
    {
        // ONE CALL, TWO FRAMES. On Darwin the pages are charged to this
        // process, so the weighed figure IS the process's footprint. On
        // Android/Linux they are not, and the only book they appear in is the
        // DEVICE's -- so what is weighed there is how much of the device is in
        // use, which moves when the renderer grows and falls when it is shed.
        const qint64 weighed = basecamp::web::budgetWeighedBytes();
        QVERIFY2(weighed > 0, qPrintable(QStringLiteral("budgetWeighedBytes() = %1").arg(weighed)));
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        const qint64 inUse =
            basecamp::web::deviceMemoryBytes() - basecamp::web::deviceAvailableBytes();
        // Two samples a few microseconds apart on a live machine, so near
        // rather than equal -- the claim is which figure it is, not that a
        // phone stood still between two reads.
        QVERIFY2(qAbs(weighed - inUse) < 64LL * 1024 * 1024,
                 qPrintable(QStringLiteral("weighed %1 vs device in use %2")
                                .arg(weighed).arg(inUse)));
        QVERIFY(weighed < basecamp::web::deviceMemoryBytes());
#else
        QVERIFY2(qAbs(weighed - basecamp::web::appResidentBytes()) < 64LL * 1024 * 1024,
                 qPrintable(QStringLiteral("weighed %1 vs app resident %2")
                                .arg(weighed).arg(basecamp::web::appResidentBytes())));
#endif
    }

    void theCeilingIsStatedInTheSameFrameAsTheFigure()
    {
        // THE DEVICE'S FRAME NEEDS THE DEVICE'S OWN LINE. A third of the device
        // is a sane ceiling for a figure that counts only this app; read against
        // a figure that counts every process it would trip the moment the phone
        // booted -- the Xiaomi idles at 1.15 GB of 2.72 GB in use and its
        // "app ceiling" was 930 MB.
        //
        // So the ceiling here is everything the device has EXCEPT the room the
        // OS wants kept free (ActivityManager.MemoryInfo.threshold, which is
        // per-device) and one page's worth of margin on top of it.
        const qint64 total = 2855644LL * 1024;         // the Xiaomi's MemTotal
        const qint64 lowLine = 300LL * 1024 * 1024;    // what the OS wants spare
        const qint64 page = 290LL * 1024 * 1024;
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceInUse(total, lowLine, page),
                 total - lowLine - page);
        // The idle device is well inside it, and three pages do not reach it.
        QVERIFY(LiveRuntimeBudget::ceilingForDeviceInUse(total, lowLine, page)
                > 1150LL * 1024 * 1024 + 310LL * 1024 * 1024);
    }

    void aDeviceThatWillNotSayItsLowLineGetsAShareInstead()
    {
        // The JNI read can fail and plain Linux has no ActivityManager at all.
        // A fraction of the device is the fallback, not "no ceiling": a ceiling
        // of zero is exactly the unreachable branch this issue is about.
        const qint64 total = 4LL * 1024 * 1024 * 1024;
        const qint64 page = 290LL * 1024 * 1024;
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceInUse(total, -1, page),
                 total - total / 8 - page);
        // ...and a device that will not say how much memory it has has nothing
        // to weigh against, which is the one honest zero.
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceInUse(-1, -1, page), Q_INT64_C(0));
        // A page that would not fit under the low line at all gives no ceiling
        // rather than a negative one.
        QCOMPARE(LiveRuntimeBudget::ceilingForDeviceInUse(256LL * 1024 * 1024, -1, page),
                 Q_INT64_C(0));
    }

    void aTightenedBudgetGrowsBackAPagesWorthUnderTheCeiling()
    {
        // HYSTERESIS IN BOTH FRAMES. It used to be three quarters of the
        // ceiling, which is a fraction that means nothing once the ceiling is
        // the DEVICE's in-use figure: on a phone that idles at 60% of its
        // ceiling, three quarters is a margin the budget can never earn back.
        // A page's worth below the ceiling is the same sentence in either
        // frame -- "there is room for another one again".
        LiveRuntimeBudget budget(3, 100LL * 1024 * 1024, 1000LL * 1024 * 1024);
        budget.show("a");
        budget.show("b");
        budget.show("c");
        budget.observe(1200LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 2);
        // 50 MB under the ceiling is not a page's worth of room.
        budget.observe(950LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 2);
        // 150 MB under it is.
        budget.observe(850LL * 1024 * 1024);
        QCOMPARE(budget.liveAllowance(), 3);
    }

    void theDeviceBudgetPairsTheFigureWithItsOwnCeiling()
    {
        // THE TWO HALVES HAVE TO COME FROM THE SAME PLACE. observe() is handed
        // budgetWeighedBytes() and compares it with the ceiling the constructor
        // was given; it cannot tell which frame either is in, so forThisDevice()
        // is the only place they can be kept together -- and a mismatch is
        // silent in exactly the way #244 was. A device-frame figure read against
        // a third-of-the-app ceiling would shed a page on the first poll of
        // every run; an app-frame figure read against a device-frame ceiling is
        // the unreachable branch this issue is about.
        const LiveRuntimeBudget budget = LiveRuntimeBudget::forThisDevice();
        const qint64 device = basecamp::web::deviceMemoryBytes();
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        QCOMPARE(budget.appCeilingBytes(),
                 LiveRuntimeBudget::ceilingForDeviceInUse(
                     device, basecamp::web::deviceLowMemoryBytes()));
#else
        QCOMPARE(budget.appCeilingBytes(), LiveRuntimeBudget::ceilingForDeviceMemory(device));
#endif
        // ...and a machine that says how much memory it has gets a ceiling,
        // which is what stops this from passing by both halves being zero.
        QVERIFY(budget.appCeilingBytes() > 0);
    }

    void theStatedCostIsOneRendererAndThenAlmostNothing()
    {
        // #230 WEIGHED IT: every page of a build shares ONE Chromium renderer.
        // 290 MB with one page live, 298 with two, 300 with three on a Xiaomi
        // 25028RN03Y; 345/345/348 on a Samsung SM-G990B. So the stated cost is
        // a renderer plus a document each, not a renderer each, and a log that
        // multiplied said 870 MB for three pages that cost about 300.
        LiveRuntimeBudget budget(3, 300LL * 1024 * 1024, 0);
        QCOMPARE(budget.budgetBytes(),
                 300LL * 1024 * 1024 + 2 * LiveRuntimeBudget::kAdditionalRuntimeBytes);
        QCOMPARE(budget.projectedBytes(), Q_INT64_C(0));
        budget.show("a");
        QCOMPARE(budget.projectedBytes(), 300LL * 1024 * 1024);
        budget.show("b");
        QCOMPARE(budget.projectedBytes(),
                 300LL * 1024 * 1024 + LiveRuntimeBudget::kAdditionalRuntimeBytes);
    }
};

QTEST_MAIN(LiveRuntimeBudgetTest)
#include "live_runtime_budget_test.moc"
