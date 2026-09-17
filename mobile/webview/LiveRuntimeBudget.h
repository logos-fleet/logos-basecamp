#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

#include <algorithm>

namespace basecamp::web {

// HOW MANY DOWNLOADED MODULES MAY HAVE A QML RUNTIME ALIVE AT ONCE.
//
// A Downloaded module on a phone is two things, and only one of them is
// expensive: a Wasm host (its own image, a few MB, answering calls) and a UI
// page (the app's bundled Qt-for-WebAssembly QML runtime, measured at 290 MB
// resident on a Samsung SM-G990B, and 2.6–3 s of cold start in the spike). The
// container keeps every installed module's Wasm host alive, because a background
// module still answers its consumers, and keeps the RUNTIME alive only for what
// the user is looking at. This is where "only for what the user is looking at"
// is decided, and what makes it a budget rather than a rule is that a tablet can
// afford more than one and a phone cannot.
//
// IT DECIDES, IT DOES NOT ACT. show() returns the modules whose UI page must be
// given up and the caller gives them up — because the caller is the only
// thing that knows what a page IS on this platform (a WKWebView, an Android
// WebView, a QWebEngineView) and because a policy that destroyed things could
// not be tested without one.
//
// EVICTION IS LEAST-RECENTLY-VISIBLE, not least-recently-loaded. The two differ
// exactly when a user goes back to something: with a load-order queue, flipping
// between two modules inside a three-module budget evicts the one being flipped
// to, which is the worst possible choice and costs the 3-second cold start on
// every flip.
class LiveRuntimeBudget {
public:
    // WHAT ONE QML RUNTIME COSTS ON A PHONE, measured rather than estimated.
    //
    // 290 MB is the resident size of the surviving renderer process on a
    // Samsung SM-G990B, sampled ten times a second from the host across a
    // module switch and read once it had settled (2026-09-12). The spike's
    // 185-240 MB was a DESKTOP browser's and is the number this used to carry;
    // the device is half as much again, and a budget derived from the
    // optimistic figure is a budget the OS disagrees with.
    //
    // It is a stated cost, not a measurement the container takes: neither phone
    // lets an embedder weigh another process (iOS offers no API for another
    // task's footprint, Android's renderer runs under a different uid), so what
    // a container can do is count pages and multiply.
    //
    // AND MULTIPLYING IS WHERE IT OVER-STATES (logos-workspace#230). Weighed
    // from the host side on 2026-09-17, every page of a build shares ONE
    // Chromium renderer: 290 MB with one page live, 298 MB with two and 300 MB
    // with three on a Xiaomi 25028RN03Y; 345/345/348 MB on a Samsung SM-G990B.
    // The figure below is right for the FIRST page and about 60x too large for
    // each one after it, so budgetBytes() states 870 MB for three pages that
    // cost about 300 MB. Left as it is rather than re-fitted here: it makes the
    // budget conservative rather than dangerous, and the count it feeds is
    // capped at three anyway. What the measurement DOES falsify is the idea
    // that the cap is a memory decision -- see kMaxLiveRuntimes.
    static constexpr qint64 kDeviceRuntimeBytes = 290LL * 1024 * 1024;

    // ...AND WHAT EVERY PAGE AFTER THE FIRST COSTS, which is almost nothing
    // (logos-workspace#244 decided this; #230 measured it).
    //
    // All of a build's pages share ONE renderer, so the figure above is the
    // price of STARTING one and this is the price of a second document in the
    // one that is already running: 290 -> 298 -> 300 MB for one, two and three
    // pages on a Xiaomi 25028RN03Y and 345 -> 345 -> 348 MB on a Samsung
    // SM-G990B, both on 2026-09-17. 8 MB is the larger of the two per-page
    // deltas rounded up.
    //
    // IT IS A STATED COST, READ ONLY BY THE LOG. budgetBytes() and
    // projectedBytes() exist so a device run can say what the container holds,
    // and multiplying the renderer's price by the page count made them say
    // 870 MB for three pages that weigh about 300 -- a log that was wrong by 3x
    // about the one thing it was added to report. The COUNT is deliberately not
    // re-fitted to this: see runtimesForDeviceMemory().
    static constexpr qint64 kAdditionalRuntimeBytes = 8LL * 1024 * 1024;

    // HOW MANY PAGES A DEVICE OF THIS SIZE MAY KEEP (#153).
    //
    // The count used to be 1 on every device, and the comment above admitted
    // why that was wrong: a tablet can afford more than one and a phone cannot,
    // and nothing asked the device which it was. This asks.
    //
    // ONE SIXTH OF THE DEVICE'S MEMORY is what the container may put into UI
    // pages, and the count is that share divided by what one page costs. The
    // fraction rather than an absolute number because the thing being defended
    // scales with the device: a phone's whole foreground allowance is a
    // fraction of its RAM on both platforms (iOS jetsam charges a footprint
    // limit that tracks the device's memory; Android's LMK kills against what
    // is left of it), and a page that is 290 MB of a 6 GB phone is a different
    // proposition from the same page on a 2 GB one.
    //
    // CAPPED AT THREE however much memory there is, for two reasons that are
    // not about memory: a user flips between two or three apps and a fourth
    // live runtime buys nothing, and the pages' real cost is in a RENDERER
    // PROCESS this app cannot weigh (see AppMemory.h) -- so every page past the
    // third is a bet against a number nobody here can read. A simulator makes
    // the same point loudly: it reports the Mac's 64 GB.
    static constexpr int kMaxLiveRuntimes = 3;
    //
    // AND THE COUNT STAYS ON THE RENDERER'S PRICE, not on kAdditionalRuntimeBytes
    // (logos-workspace#244). Divided by 8 MB every device the venue has would
    // afford the cap, and the count would stop being a device question at all --
    // which is the fixed number #153 removed, arrived at from the other side.
    // What a second page really costs a user is a second live document in a
    // renderer that is already the app's largest single allocation, plus the
    // cold start of getting it there; the bytes are the smaller half of that.
    // The count is the conservative half of this policy and the MEASUREMENT is
    // the half that acts -- and as of #244 the measurement can finally trip.
    static int runtimesForDeviceMemory(qint64 deviceMemoryBytes,
                                       qint64 runtimeFootprintBytes = kDeviceRuntimeBytes);

    // WHAT THE APP AS A WHOLE MAY WEIGH on a device of this size, which is a
    // different number from the pages' share above: this covers the core, the
    // modules, the QML scene and the pages together, because
    // appResidentBytes() cannot separate them.
    //
    // A THIRD OF THE DEVICE, and the point of the fraction is that it is UNDER
    // what the OS acts on rather than at it: iOS jetsam's foreground limit is
    // roughly half a device's memory, and a container that shed its first page
    // there would be shedding at the moment it was already being killed. A
    // third leaves a page's worth of margin on every device the venue has.
    //
    // Zero means "there is nothing to weigh against" -- a device that will not
    // say its memory -- and a budget with a ceiling of zero counts and does not
    // weigh, which is what this class did before #153.
    //
    // THE FRAME IS THE PROCESS'S, so this is the ceiling for a platform whose
    // pages are charged to the process that opened them -- iOS and macOS. See
    // ceilingForDeviceInUse() for Android's, and AppMemory::budgetWeighedBytes()
    // for why there are two.
    static qint64 ceilingForDeviceMemory(qint64 deviceMemoryBytes);

    // ...AND THE SAME QUESTION IN THE DEVICE'S FRAME (logos-workspace#244).
    //
    // On Android what observe() weighs is HOW MUCH OF THE DEVICE IS IN USE,
    // because that is the only book a `web` page appears in, so the ceiling has
    // to be a level of device use rather than a share of the app. A third of
    // the device would be nonsense here: the Xiaomi 25028RN03Y idles with
    // 1.15 GB of its 2.72 GB in use and would evict on its first observation,
    // every run, before a page had been opened.
    //
    // EVERYTHING EXCEPT THE ROOM THE OS WANTS KEPT FREE, AND A PAGE'S MARGIN.
    // `deviceLowMemoryBytes` is Android's own per-device line (AppMemory.h) --
    // the level at which the system starts killing background processes -- and
    // the extra page's worth is so the container sheds BEFORE the OS does: at
    // the line itself, the thing that gets reaped is the shared renderer, and
    // then every page goes at once instead of the least recently visible one.
    //
    // A DEVICE THAT WILL NOT SAY ITS LINE GETS AN EIGHTH OF ITSELF, which is
    // the order of what Android's own threshold comes to on the venue's phones,
    // and a device that will not say its memory gets 0 -- no ceiling, count
    // only, which is the one honest answer to "weigh this against nothing".
    static qint64 ceilingForDeviceInUse(qint64 deviceMemoryBytes,
                                        qint64 deviceLowMemoryBytes,
                                        qint64 runtimeFootprintBytes = kDeviceRuntimeBytes);

    // THE BUDGET THIS DEVICE GETS, measured rather than assumed. The count and
    // the ceiling both come from deviceMemoryBytes().
    //
    // A RUN CAN STATE THE COUNT, and has to be able to: measuring what two and
    // three live runtimes cost on a phone whose policy says one is exactly the
    // measurement #153 asks for, and a policy that could not be overridden
    // could not be checked. `--web-budget <n>` on the app's own command line,
    // or `LOGOS_WEB_RUNTIME_BUDGET` where there is a shell to set one -- the
    // flag because there is no environment to speak of on a phone: an APK's
    // process inherits nothing a developer typed, while the launcher forwards
    // arguments on both platforms.
    //
    // ...AND SO CAN THE CEILING, `--web-ceiling <MB>` or
    // `LOGOS_WEB_APP_CEILING_MB` (#244). A ceiling that cannot be reached is
    // indistinguishable from one that never needed to be, which is exactly how
    // #153's unreachable branch survived a landing: a run has to be able to put
    // the ceiling where this device will cross it and watch the eviction it
    // causes. Stated in MB, against whatever frame this platform weighs.
    static LiveRuntimeBudget forThisDevice(const QStringList& args = {});

    // One runtime is the DEFAULT still, and deliberately: a host that states no
    // number gets the conservative one rather than a guess, and the hosts that
    // ship on a phone call forThisDevice() instead.
    //
    // `appCeilingBytes` of zero is "no ceiling": observe() then changes nothing,
    // whatever it is told.
    explicit LiveRuntimeBudget(int maxLiveRuntimes = 1,
                               qint64 runtimeFootprintBytes = kDeviceRuntimeBytes,
                               qint64 appCeilingBytes = 0);

    // `module` is now the visible Downloaded module. Returns the modules whose
    // UI page the caller must drop to stay inside the budget, LEAST RECENTLY
    // VISIBLE FIRST — which is also the order they must be destroyed in for a
    // log of the run to read like what happened.
    //
    // Showing what is already visible evicts nothing: a repeated tab press must
    // not cost a teardown and a cold start.
    QStringList show(const QString& module);

    // The module's UI page is gone for a reason of its own — it was unloaded,
    // its page died, the user uninstalled it. Idempotent, and that matters:
    // this is what stops a page the container has already destroyed from being
    // named for eviction a second time, which on every platform here is a use
    // after free rather than a no-op.
    void forget(const QString& module);

    // WHAT THE PLATFORM WEIGHS RIGHT NOW, answered rather than logged (#153).
    //
    // `weighedBytes` is AppMemory::budgetWeighedBytes() -- this process's
    // footprint on iOS, how much of the DEVICE is in use on Android -- or -1
    // where the platform will not say, and a platform that will not say is not
    // a reason to evict, so -1 changes nothing. It must be read against a
    // ceiling stated in the same frame; this class does not know which frame it
    // is in and does not need to, because both sides of the comparison come
    // from forThisDevice().
    //
    // ONE PAGE PER OBSERVATION, least recently visible first: the number is
    // never the page's own on either platform (AppMemory.h), so the container
    // cannot know which page is the expensive one, and shedding the lot on one
    // reading would cost three cold starts to answer a spike that was passing.
    //
    // Never the visible module: one page is the floor here exactly as it is in
    // the constructor.
    QStringList observe(qint64 weighedBytes);

    // THE OS ASKED FOR MEMORY BACK, which is the one signal in this file that
    // is not this app's own opinion. Everything but the module the user is
    // looking at gives its page up, at once rather than one per observation:
    // iOS kills an app rather than ask it twice.
    //
    // The allowance stays where this left it until a later observe() finds the
    // app well under its ceiling again -- a warning answered and then
    // immediately forgotten would refill the app to the size the OS just
    // complained about.
    QStringList shedUnderPressure();

    // HOW MANY PAGES MAY LIVE RIGHT NOW, which is the device's number until a
    // measurement or a warning tightens it and again once the app is small
    // enough for it to be relaxed. maxLiveRuntimes() is what the device
    // affords; this is what the app has earned.
    int liveAllowance() const { return m_allowance; }

    // Live UI runtimes, most recently visible first.
    QStringList live() const { return m_live; }
    bool isLive(const QString& module) const { return m_live.contains(module); }

    // The module whose UI is on screen, or empty when nothing is.
    QString visible() const { return m_visible; }

    int maxLiveRuntimes() const { return m_maxLive; }

    // THE STATED BUDGET, in bytes, and what the container is currently holding
    // against it. Both exist to be LOGGED: slice 28 asks the host to say what
    // its budget is and to show that switching modules brings the app back
    // inside it, and a number the host never prints cannot be checked from a
    // device run.
    // ONE RENDERER AND THEN A DOCUMENT EACH, not a renderer each. See
    // kAdditionalRuntimeBytes: the pages share one process, and stating them as
    // a multiple of the first page's price over-reported three live runtimes by
    // about 3x in every device log this container has printed.
    qint64 budgetBytes() const { return statedCostOf(m_maxLive); }
    qint64 projectedBytes() const { return statedCostOf(int(m_live.size())); }
    qint64 runtimeFootprintBytes() const { return m_footprintBytes; }
    qint64 appCeilingBytes() const { return m_ceilingBytes; }

private:
    // Trim to the current allowance, least recently visible first.
    QStringList trimToAllowance();
    // What `pages` live runtimes are stated to cost. Zero pages cost nothing;
    // the first pays for the renderer and the rest for a document each.
    qint64 statedCostOf(int pages) const
    {
        return pages <= 0 ? 0 : m_footprintBytes + qint64(pages - 1) * kAdditionalRuntimeBytes;
    }
    // WHERE A TIGHTENED BUDGET IS ALLOWED TO GROW AGAIN. A page's worth below
    // the ceiling: a budget that relaxed the moment the reading dipped under the
    // ceiling would follow a number that moves by megabytes between two frames,
    // and the user would pay a 3-second cold start for each oscillation.
    //
    // A PAGE'S WORTH RATHER THAN A FRACTION (logos-workspace#244), because it is
    // the same sentence in both frames -- "there is room for another one again"
    // -- and three quarters of the ceiling is not: on Android the ceiling is a
    // level of DEVICE use and a phone that idles above three quarters of it
    // could never earn its pages back.
    qint64 relaxBelowBytes() const { return std::max<qint64>(0, m_ceilingBytes - m_footprintBytes); }

    int m_maxLive;
    int m_allowance;
    qint64 m_footprintBytes;
    qint64 m_ceilingBytes;
    // Most recently visible first. Short by construction (the budget is 1 on a
    // phone), so a list beats anything with a bucket in it.
    QStringList m_live;
    QString m_visible;
};

} // namespace basecamp::web
