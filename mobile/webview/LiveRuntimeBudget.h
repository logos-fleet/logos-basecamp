#pragma once

#include <QString>
#include <QStringList>

#include <QtGlobal>

namespace basecamp::web {

// HOW MANY DOWNLOADED MODULES MAY HAVE A QML RUNTIME ALIVE AT ONCE.
//
// A Downloaded module on a phone is two things, and only one of them is
// expensive: a Wasm host (its own image, a few MB, answering calls) and a UI
// page (the app's bundled Qt-for-WebAssembly QML runtime, measured at 185–240 MB
// resident and 2.6–3 s of cold start in the spike). The container keeps every
// installed module's Wasm host alive, because a background module still answers
// its consumers, and keeps the RUNTIME alive only for what the user is looking
// at. This is where "only for what the user is looking at" is decided, and what
// makes it a budget rather than a rule is that a tablet can afford more than one
// and a phone cannot.
//
// IT DECIDES, IT DOES NOT ACT. show() returns the modules whose UI page must be
// torn down and the caller tears them down — because the caller is the only
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
    // What one QML runtime cost in the spike, at the top of the measured range.
    // The pessimistic end deliberately: a budget derived from the optimistic one
    // is a budget the OS disagrees with.
    static constexpr qint64 kSpikeRuntimeBytes = 240LL * 1024 * 1024;

    // One runtime is the phone's answer and the DEFAULT, because 240 MB is
    // already most of what a mid-range phone will let a foreground app keep.
    explicit LiveRuntimeBudget(int maxLiveRuntimes = 1,
                               qint64 runtimeFootprintBytes = kSpikeRuntimeBytes);

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
    qint64 budgetBytes() const { return qint64(m_maxLive) * m_footprintBytes; }
    qint64 projectedBytes() const { return qint64(m_live.size()) * m_footprintBytes; }
    qint64 runtimeFootprintBytes() const { return m_footprintBytes; }

private:
    int m_maxLive;
    qint64 m_footprintBytes;
    // Most recently visible first. Short by construction (the budget is 1 on a
    // phone), so a list beats anything with a bucket in it.
    QStringList m_live;
    QString m_visible;
};

} // namespace basecamp::web
