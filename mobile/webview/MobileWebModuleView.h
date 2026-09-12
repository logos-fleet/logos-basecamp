#pragma once

#include "webview/MobileWebBridge.h"

#include <web_module_view.h>   // LogosCore::WebModuleView — liblogos' seam

#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace basecamp::web {

// ONE DOWNLOADED MODULE'S PAGE ON A PHONE, as liblogos' Web container sees it.
//
// The container asks for somewhere to run a page and gets this; what it uses is
// a message channel that can die, and nothing else (see WebModuleView). So the
// platform half of a phone container is ONLY the two things liblogos cannot
// express: the webview itself, and where its pixels go.
//
// THE SPLIT THIS CLASS DRAWS. Everything about what the page may fetch and how
// its frames cross is MobileWebBridge's and is tested on a desktop with no
// webview in it (tests/mobile_web_bridge_test.cpp). What is left — and what
// each platform implements — is:
//
//   * make a webview whose requests for `logos://module/...` arrive at
//     bridge->handleRequest(), and whose replies stream a named file;
//   * inject bridge->channelShim() before the page's own scripts;
//   * load bridge->entryUrl();
//   * report the page's death.
//
// `PlatformPage` is that, and it is a struct of callbacks rather than a base
// class because the iOS implementation is Objective-C++ and the Android one is
// JNI: neither can be a C++ subclass without dragging its platform's headers
// into every consumer of this one.
struct PlatformPage {
    // Tear the webview down. Called exactly once, from the Qt main thread.
    std::function<void()> destroy;
    // The platform's own handle for the surface — a UIView* on iOS, a jobject
    // on Android — for the shell to mount. Opaque here on purpose: the layer
    // that mounts it is the layer that knows what it is.
    std::function<void*()> nativeHandle;
    // Whether the webview still exists. A renderer that was killed (iOS
    // `webViewWebContentProcessDidTerminate`, Android `onRenderProcessGone`)
    // reports through onDied instead; this is for the ordinary question.
    std::function<bool()> isAlive;
    // PUT THIS PAGE IN FRONT OF THE HOST'S OWN SURFACE, or behind it again.
    //
    // Both phones mount a module's page at the BACK of the view hierarchy at
    // creation, for the same reason: a webview that is not in a window is
    // throttled -- requestAnimationFrame stops and a Qt-wasm QML runtime
    // drawing through it freezes -- so a page has to be mounted before anyone
    // decides to look at it. Which means "the user is looking at this module"
    // is a separate instruction, and this is it.
    //
    // Optional: a platform that has no z-order to speak of (and the fake webview
    // a desktop test drives) leaves it unset, and the backend then only keeps
    // the books.
    std::function<void(bool front)> setFrontmost;
    // RUN ONE SCRIPT IN THE PAGE. Fire and forget: what the script has to say
    // it says through the page's own console, which the bridge already carries
    // back (`[webView evaluateJavaScript:...]`, `WebView.evaluateJavascript`).
    //
    // It exists for ONE reason and it is worth stating, because a seam that
    // runs arbitrary JavaScript in a module's page invites others. A `web`
    // variant draws into a canvas: there is no DOM node to touch and no text
    // node to read, so a host that wants to show that a real key or a real
    // finger reaches the view has no way to deliver one except through the
    // page. Every other conversation with a module goes down the channel.
    //
    // Optional. A platform that cannot do it leaves it unset and a host that
    // asks says so rather than reporting a silent success.
    std::function<void(const QString& script)> evaluateJavaScript;
};

// What a platform backend is asked to build. Everything it needs and nothing
// about the module, so a platform half never has to know what a Logos module is.
struct PlatformPageRequest {
    QString moduleName;
    QUrl entryUrl;
    QString channelShim;
    // Answer one request from the page. Safe to call from the platform's UI
    // thread; the Respond callback may be invoked later, from another thread,
    // and the platform half is responsible for hopping back if its API demands
    // it (a WKURLSchemeTask may only be completed on the thread that started
    // it).
    std::function<void(const QByteArray& method, const QUrl& url,
                       const QByteArray& body, MobileWebBridge::Respond)> serve;
    // The page went away for a reason the platform noticed.
    std::function<void()> onDied;
};

using PlatformPageFactory = std::function<PlatformPage(const PlatformPageRequest&)>;

// The view liblogos gets. Platform-free: it owns the bridge, asks the installed
// platform factory for a page, and is the channel's host end.
class MobileWebModuleView : public LogosCore::WebModuleView {
public:
    MobileWebModuleView(const LogosCore::WebModuleViewRequest& request,
                        const QString& runtimeDir,
                        const PlatformPageFactory& platform,
                        bool shimInDocument = false,
                        WebOrigin origin = {});
    ~MobileWebModuleView() override;

    logos::web::MessageChannelPtr channel() const override;
    // NO PID, deliberately, and not because one could not be found: iOS runs
    // every WKWebView's content in its own WebContent process and will name it.
    // A web module's pid is what the shell shows as "the module's process", and
    // that process is shared machinery the host owns, not the module's own —
    // the same answer, for the same reason, as InProcContainer::kInProcPid.
    void setOnDied(std::function<void()> callback) override;
    bool isAlive() const override;

    const QString& moduleName() const { return m_moduleName; }

    // The page could not be brought up at all. The container's own verdict is
    // still "the page published no module", which is the same outcome; this is
    // what says WHY in the log.
    const QString& startupError() const { return m_startupError; }

    // The platform's handle for the surface, for the shell to mount. Null when
    // the page never came up.
    void* nativeHandle() const;

    // Bring this page in front of the host's own surface, or send it back.
    // A no-op on a platform whose page does not implement it.
    void setFrontmost(bool front);

    // ── the live-runtime budget's other half ───────────────────────────────
    //
    // GIVE THE UI UP AND KEEP THE MODULE SERVING. A `ui_qml` module's `web`
    // variant ships two entry documents (logos-module-builder's
    // buildWebViewModule.nix): `index.html`, which is the app's ~26 MB QML
    // runtime plus this module's view plus its Wasm host, and a headless one,
    // which is the Wasm host alone. A module the user has navigated away from
    // is swapped onto the second: its page stops costing a runtime and the
    // module keeps answering calls.
    //
    // THE CHANNEL DOES NOT MOVE, and that is what makes this safe to do behind
    // the container's back. This view object, its bridge and the channel the
    // core holds are all untouched; what changes is which document the webview
    // is showing. The core is never told, because from the core's side nothing
    // happened -- a Call made across the swap is buffered by the bridge exactly
    // as the first Subscribe is buffered while a page boots, and is answered
    // when the new image polls.
    //
    // WHAT IS LOST, said plainly: the outgoing image's state goes with it. A
    // `web` module's backend lives in the page, so an eviction is a restart of
    // the module's host -- its properties come back at their defaults and a
    // subscription the container made is not repeated. Calls are answered; a
    // module that has to keep in-memory state across a background trip has to
    // keep it somewhere the page is not.
    bool hasUi() const { return m_hasUi; }

    // Swap this page onto the headless document, or back onto the UI one.
    // False when the swap could not be made -- no headless document in the
    // package (a variant built before they existed ships none, and the honest
    // answer for one of those is for the host to unload the module rather than
    // to guess at a file name), or the platform could not open the new page --
    // and the caller is then holding a module whose UI it must dispose of some
    // other way.
    bool evictUi();
    bool restoreUi();

    // Run one script in the page. False when this platform's page cannot.
    bool runJavaScript(const QString& script);

    // SEND ONE PROTOCOL FRAME INTO THIS MODULE'S PAGE, and watch what comes
    // back (`observeFrames`, whose sink stays installed until an empty one
    // replaces it).
    //
    // The frame goes down the SAME channel the core uses -- that is the point:
    // what this shows is the module answering on its real wire, not on a second
    // one built for the occasion. It exists because a host cannot ask the
    // question any other way: neither ICoreRuntime nor liblogos' C API has a
    // call-a-module entry point, and the only callers in the system are other
    // modules. A phone host that has to show that a BACKGROUNDED module is
    // still answering has no module to borrow.
    //
    // The core's own client sees the answer too and drops it, having no
    // outstanding request with that id.
    void observeFrames(std::function<void(const QString& frame)> sink);
    bool sendFrame(const QString& frame);

    // Every line the page's own console produced. Already printed through Qt's
    // message handler; this is for a host that wants to WAIT for one -- which
    // for a view that draws into a canvas is the only way to know it came up.
    // Set before the page is loaded, so nothing is missed.
    void setOnPageLog(std::function<void(const QString& level, const QString& message)> sink)
    {
        m_onPageLog = std::move(sink);
    }

    // Called from the destructor, whatever destroyed this view. The container
    // owns the view's lifetime and the backend owns the registry of them, and
    // this is the only place the two meet: a view the container has destroyed
    // must not stay in the registry, and the backend cannot ask.
    void setOnDestroyed(std::function<void()> callback);

    // Answer every long poll the page has parked, so an idle module is not
    // holding a request the webview is waiting on. Driven by the backend's
    // timer, not by this class -- there is no event loop in here.
    void expireWaits();

private:
    void announceDeath();
    // Open a page on one of this package's documents, against the bridge that
    // is already there. Everything the platform is told is derived from the
    // bridge, so the two entry documents differ in exactly one argument.
    bool openPage(const QString& entryFile);
    bool swapPageTo(const QString& entryFile);

    QString m_moduleName;
    QString m_startupError;
    std::shared_ptr<MobileWebBridge> m_bridge;
    logos::web::MessageChannelPtr m_channel;
    PlatformPage m_page;
    // Held, because a swap builds a second page from the same ingredients.
    PlatformPageFactory m_platform;
    bool m_shimInDocument = false;
    QString m_uiEntry;
    QString m_headlessEntry;
    bool m_hasUi = true;
    // A page torn down on purpose must not be reported as a page that died.
    // Same reason as the destructor's m_announced, at a smaller scale.
    bool m_swapping = false;
    std::function<void()> m_onDied;
    std::function<void(const QString&, const QString&)> m_onPageLog;
    std::function<void()> m_onDestroyed;
    bool m_announced = false;
    bool m_alive = true;
};

} // namespace basecamp::web
