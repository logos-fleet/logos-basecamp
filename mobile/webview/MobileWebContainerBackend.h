#pragma once

#include "webview/LiveRuntimeBudget.h"
#include "webview/MobileWebModuleView.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

namespace basecamp::web {

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
// EVICTION IS ANNOUNCED, NOT PERFORMED, and that is the same ownership rule
// again: the page belongs to liblogos' container, which destroys it when the
// module is unloaded. A backend that destroyed a view behind the container's
// back would leave a published module with a dead channel. So show() emits
// uiEvictionRequired and the host unloads the module through the core, which
// tears the view down through the ordinary path.
//
// KNOWN LIMIT, and it is the artifact's rather than this class's: a `ui_qml`
// module's `web` variant today is ONE page carrying both the QML runtime and
// the module's own Qt-wasm backend image (logos-module-builder's
// buildWebViewModule.nix), so giving up the UI gives up the Wasm host with it
// and the module stops answering calls until it is shown again. Slice 28 asks
// for a background module to keep answering, and that needs the variant to ship
// a SECOND, headless entry document — the Bare `web` variant's loader page —
// with the container relaying between the two. Nothing here has to change when
// it does: the budget already governs UI pages only.
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
    QStringList loadedModules() const;

    // THE SHELL SAYS WHAT THE USER IS LOOKING AT. Returns the Downloaded modules
    // whose UI page must be given up to stay inside the budget, least recently
    // visible first, and emits uiEvictionRequired for each. Logs the budget and
    // what is held against it either way — slice 28 asks for the number, and a
    // number the host never prints cannot be read off a device run.
    QStringList show(const QString& moduleName);

    const LiveRuntimeBudget& budget() const { return m_budget; }

signals:
    // A module's page exists and can be mounted. Emitted BEFORE the container
    // asks the page whether it is serving, so the page is on screen while it
    // boots — a 26 MB runtime takes long enough that a shell which waited for
    // the load verdict would show nothing at all for seconds.
    void viewOpened(const QString& moduleName, void* nativeHandle);

    // The module's page is going away. The handle is already unusable.
    void viewClosed(const QString& moduleName);

    // This module is over the live-runtime budget and must give its page up.
    // The host answers by unloading it through the core; see the class note on
    // why this backend does not do it itself.
    void uiEvictionRequired(const QString& moduleName);

private:
    MobileWebContainerBackend() = default;

    // Called by the factory, always on the Qt main thread.
    MobileWebModuleView* createView(const LogosCore::WebModuleViewRequest& request,
                                    const QString& runtimeDir);
    bool m_shimInDocument = false;
    WebOrigin m_origin;
    void forget(const QString& moduleName);
    void armPollTimer();

    QHash<QString, MobileWebModuleView*> m_views;
    LiveRuntimeBudget m_budget;
    PlatformPageFactory m_platform;
    QTimer* m_pollTimer = nullptr;
};

} // namespace basecamp::web
