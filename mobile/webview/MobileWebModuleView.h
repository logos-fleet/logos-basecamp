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
                        bool shimInDocument = false);
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

    QString m_moduleName;
    QString m_startupError;
    std::shared_ptr<MobileWebBridge> m_bridge;
    logos::web::MessageChannelPtr m_channel;
    PlatformPage m_page;
    std::function<void()> m_onDied;
    std::function<void()> m_onDestroyed;
    bool m_announced = false;
    bool m_alive = true;
};

} // namespace basecamp::web
