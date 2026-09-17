#pragma once

#include "webview/LiveRuntimeBudget.h"
#include "webview/MobileWebModuleView.h"

#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>

class QTimer;

namespace basecamp::web {

// WHERE THIS APP'S BUNDLED Qt-wasm QML RUNTIME IS, or an empty string when it
// ships none — `install()`'s first argument.
//
// `platformDir` is where this platform unpacks it (iOS: the main bundle's
// Resources; Android: the app's files directory), which is the only thing the
// two halves disagree about. `LOGOS_QML_RUNTIME_DIR` overrides it, and a
// directory with no `logos_qml_runtime.js` in it is not one whatever named it.
QString bundledQmlRuntimeDir(const QString& platformDir);

// THE PHONE'S WEB CONTAINER BACKEND — the one place that says "a Downloaded
// module's page runs here", and the one place that says how many may.
//
// liblogos registers the Web container in every process and leaves the webview
// unset, so a process with no browser in it reports the missing bridge instead
// of "no loader for this format". This installs one: after install(), loading a
// `web` module through the ordinary core path opens a real page and the
// container publishes it under its own identity, exactly as it publishes a
// subprocess module on the desktop.
//
// IT ALSO HOLDS THE VIEWS, because two different owners need them and neither
// can ask the other. The container owns each page's LIFETIME (it destroys the
// view when the module unloads or its page dies) while the shell owns where the
// page GOES ON SCREEN — and the container has no seam through which to hand a
// surface out. So the factory reports here, this announces it, and the shell
// mounts what it is given and drops it when it is told to. Same division, and
// the same reason, as the desktop WebContainerBackend.
//
// WHAT IS NEW ON A PHONE IS THE BUDGET. The Qt-wasm QML runtime measured 185–240
// MB resident per page; a shell that kept one per installed module would be
// killed by the OS. So the shell tells this backend which Downloaded module the
// user is looking at, and LiveRuntimeBudget names the ones that must give their
// page up.
//
// WHAT AN EVICTION DOES depends on what the module's package ships, and the two
// answers are different in kind.
//
//   * A variant with a HEADLESS entry document is BACKGROUNDED: this backend
//     swaps its page from the UI document to the headless one, the module keeps
//     its channel and keeps answering calls, and uiEvicted says so. Nothing is
//     done behind liblogos' back — the view object, its bridge and the channel
//     the core holds are all untouched, and the core is never told because from
//     its side nothing happened.
//   * A variant WITHOUT one has nowhere to go, and then eviction is announced
//     and not performed: uiEvictionRequired fires and the host unloads the
//     module through the core, which tears the view down through the ordinary
//     path. A backend that destroyed a view behind the container's back would
//     leave a published module with a dead channel.
//
// The second is the older answer and is kept for the packages that need it —
// every `web` variant built before logos-module-builder emitted a second
// document.
class MobileWebContainerBackend : public QObject {
    Q_OBJECT
public:
    // Process-wide, because the seam it fills is
    // (LogosCore::setWebModuleViewFactory is a process-global). One owner, one
    // registry, and the shell has one object to connect to.
    static MobileWebContainerBackend* instance();

    // Install the factory.
    //
    //   runtimeDir  where this app keeps its bundled Qt-wasm QML runtime. Empty
    //               is allowed and means "this build ships none": a `web`
    //               variant whose loader asks for one then fails IN THE PAGE
    //               with a message naming what is missing, which beats a blank
    //               rectangle.
    //   platform    how this platform makes a webview (iOS: WKWebView; Android:
    //               android.webkit.WebView).
    //   budget      how many QML runtimes may be alive at once.
    //   shimInDocument
    //               serve the channel shim inside the entry document instead of
    //               injecting it. Android's answer; see
    //               MobileWebBridge::setInjectsShimIntoHtml.
    //   origin      what the page is served on. Android needs
    //               WebOrigin::android(); see LogosWebPaths.h.
    //
    // MUST BE CALLED ON THE QT MAIN THREAD, which it then remembers: a webview
    // may only be built on the platform's UI thread, while the core may load a
    // module from its own owner thread.
    void install(const QString& runtimeDir, PlatformPageFactory platform,
                 const LiveRuntimeBudget& budget = LiveRuntimeBudget(),
                 bool shimInDocument = false,
                 WebOrigin origin = {});

    // The platform handle a loaded web module's page draws into, or nullptr.
    void* nativeHandleFor(const QString& moduleName) const;
    bool hasView(const QString& moduleName) const;
    // Whether that view is showing the module's UI document rather than its
    // headless one. A backgrounded module HAS a view and HAS no UI, and the two
    // questions have different answers for the first time in this class.
    bool hasUiPage(const QString& moduleName) const;
    // Whether that module's page is its UI AT ALL, which is a fact about the
    // package rather than about what the page is showing right now. A `core`
    // module's `web` variant has a page -- a wasm image needs a document -- and
    // no user interface. See MobileWebModuleView::servesUi.
    bool pageServesUi(const QString& moduleName) const;
    QStringList loadedModules() const;

    // Send one logos-protocol frame into a module's page and observe what comes
    // back — the only way a host can ask a module a question, since a call-a-
    // module entry point exists nowhere below this and the only callers in the
    // system are other modules. See MobileWebModuleView::callIntoPage.
    void observeFramesFrom(const QString& moduleName,
                           std::function<void(const QString& frame)> sink);
    bool sendFrameTo(const QString& moduleName, const QString& frame);

    // Run one script in a module's page. False when there is no such page or
    // the platform cannot. The only caller is a host driving real input at a
    // view that draws into a canvas — see PlatformPage::evaluateJavaScript.
    bool runJavaScriptIn(const QString& moduleName, const QString& script);

    // THE SHELL SAYS WHAT THE USER IS LOOKING AT. Returns the Downloaded modules
    // that gave their UI page up to stay inside the budget, least recently
    // visible first. Each is either backgrounded here (uiEvicted) or handed to
    // the host to unload (uiEvictionRequired); see the class note.
    //
    // It also brings the shown module's UI BACK if it had been backgrounded,
    // which is the other half of the same rule: a budget that only ever took
    // pages away would leave a user staring at the module they just chose.
    //
    // Logs the budget and what is held against it either way — slice 28 asks
    // for the number, and a number the host never prints cannot be read off a
    // device run.
    QStringList show(const QString& moduleName);

    // THE APP HAS GROWN, OR THE OS HAS ASKED (#153).
    //
    // memoryWarning() is the platform's own signal arriving -- iOS's
    // UIApplicationDidReceiveMemoryWarningNotification, Android's onTrimMemory
    // at a level that means "you are next" -- and it keeps only the page the
    // user is looking at. It is subscribed to in install(), through
    // watchAppMemoryPressure(), and is public because a host and a test both
    // have reason to fire it.
    //
    // observeMemory() is the other, quieter half: what this PLATFORM weighs
    // right now, read against the budget's ceiling. The container calls it
    // whenever a page is shown and on the poll timer's cadence while pages are
    // live, which is the only way growth inside a page is ever noticed --
    // nothing else in this class wakes up between two taps. `weighedBytes` is
    // AppMemory::budgetWeighedBytes(), or -1 for a platform that will not say,
    // and -1 changes nothing.
    //
    // IT IS NOT ALWAYS THIS PROCESS'S FIGURE (#244). On Android a `web` page
    // lives in a Chromium renderer of its own, so what is weighed there is how
    // much of the DEVICE is in use -- the only book the page appears in. The
    // caller passes budgetWeighedBytes() and the ceiling install() was given is
    // stated in the matching frame; the pair is chosen together in
    // LiveRuntimeBudget::forThisDevice().
    //
    // Both answer with the modules whose UI page was given up, and both give it
    // up the same way a count eviction does (uiEvicted / uiEvictionRequired):
    // there is one way out of a page in this container, whatever decided.
    QStringList memoryWarning();
    QStringList observeMemory(qint64 weighedBytes);

    // NOBODY IS LOOKING AT A MODULE. Every page goes behind the host's own
    // surface and the budget's books are not touched: closing an app is not
    // showing another one, and it is not an eviction -- the module stays loaded
    // and keeps answering, its page simply stops covering the Shell.
    //
    // It cannot be spelled `show({})`: that would enter a visible module under a
    // name no module has, and the next real show() would evict against a
    // phantom. Z-order rather than visibility, for the reason in the class note
    // -- a hidden webview is throttled and a background module needs its timers.
    void hideAll();

    // WHERE A PAGE IS ALLOWED TO BE, in the host window's coordinates and in Qt
    // logical pixels. Applies to every page this container holds, now and
    // later.
    //
    // THIS IS THE ANSWER TO #110. A page is mounted at the WINDOW's size, so a
    // page brought forward covers the host's own chrome -- on a phone that is
    // the sidebar, the navigation bar and the app's own close button, and a
    // user who opened a web app could not leave it again. The Shell docks a
    // placeholder for a web app exactly as it docks a `ui_qml` widget and
    // publishes the rect the workspace gave it; the page is inset to that rect
    // and the chrome stays outside it, so both kinds of app have one navigation
    // model.
    //
    // EMPTY MEANS THE WHOLE WINDOW, which is where a page starts and what it
    // goes back to when no app is docked. It is not "hide": a page that is not
    // in front is not seen whatever its rect, and hideAll() is still the
    // z-order half.
    void setContentRect(const QRect& windowRect);
    const QRect& contentRect() const { return m_contentRect; }

    // WHOSE PAGE IS IN FRONT OF THE HOST'S OWN SURFACE, or empty when none is.
    //
    // NOT budget().visible(), which is the books: hideAll() takes every page off
    // screen and deliberately does not touch them, so the budget still names the
    // last module the user looked at. This is the surface, and it is what a host
    // asserts on when it has to show that leaving an app really put the Shell
    // back (#110).
    const QString& frontmostModule() const { return m_frontmost; }

    const LiveRuntimeBudget& budget() const { return m_budget; }

    // WHAT THE APP WEIGHS RIGHT NOW, as one log line. show() prints it, and the
    // host prints it again once it has answered an eviction -- which is the
    // pair slice 28 asks for: the memory a shell holds with a module's UI live,
    // and what it returns to when that UI is given up. A platform that will not
    // say says so rather than printing a zero.
    static QString appMemoryLine(const QString& occasion);

signals:
    // A module's page exists and can be mounted. Emitted BEFORE the container
    // asks the page whether it is serving, so the page is on screen while it
    // boots — a 26 MB runtime takes long enough that a shell which waited for
    // the load verdict would show nothing at all for seconds.
    void viewOpened(const QString& moduleName, void* nativeHandle);

    // The module's page is going away. The handle is already unusable.
    void viewClosed(const QString& moduleName);

    // One line the module's page wrote to its own console. A `web` variant
    // draws into a canvas, so what it says is the only thing outside it can
    // read -- a host that asserts "the view came up" asserts on these.
    void pageLog(const QString& moduleName, const QString& level, const QString& message);

    // This module is over the live-runtime budget and has been BACKGROUNDED:
    // its UI page is gone and its Wasm host is running on the package's
    // headless document. The module is still loaded and still answers calls;
    // the host has nothing to do but unmount the surface it was given.
    void uiEvicted(const QString& moduleName);

    // This module is over the live-runtime budget and has nowhere to go — its
    // package ships no headless document — so it must give its page up by being
    // unloaded. The host answers by unloading it through the core; see the
    // class note on why this backend does not do it itself.
    void uiEvictionRequired(const QString& moduleName);

private:
    MobileWebContainerBackend() = default;

    // Called by the factory, always on the Qt main thread.
    MobileWebModuleView* createView(const LogosCore::WebModuleViewRequest& request,
                                    const QString& runtimeDir);
    void forget(const QString& moduleName);
    void armPollTimer();
    // Give up the pages the budget named, by the only two routes there are.
    // `because` is the half-sentence the log line is built round, so a device
    // run says WHY a page went -- over the count, over the ceiling, or asked
    // for by the OS.
    void applyEvictions(const QStringList& evicted, const QString& because);

    // WHAT THE BOOKS SAY RIGHT NOW, as the tail of a log line: how many UI
    // runtimes are alive and what they weigh against the budget. Three lines
    // state it -- a page shown, a page gone, and a show() for a module with no
    // page -- and one wording is what lets a device run's console be read as
    // one account.
    QString budgetLine() const;

    QHash<QString, MobileWebModuleView*> m_views;
    LiveRuntimeBudget m_budget;
    QRect m_contentRect;
    QString m_frontmost;
    PlatformPageFactory m_platform;
    bool m_shimInDocument = false;
    WebOrigin m_origin;
    QTimer* m_pollTimer = nullptr;
    // Subscribed once, however many times install() is called: the platform's
    // notification outlives an install and a second subscription would answer
    // one warning twice.
    bool m_watchingPressure = false;
};

} // namespace basecamp::web
