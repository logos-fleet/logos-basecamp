#include "webview/IosWebPage.h"

#include "webview/MobileWebBridge.h"
#include "webview/MobileWebContainerBackend.h"
#include "web/LogosWebPaths.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

#include <QDebug>
#include <QGuiApplication>
#include <QWindow>
#include <QDir>
#include <QFileInfo>

#include <memory>

namespace {

using basecamp::web::BridgeReply;
using basecamp::web::MobileWebBridge;

using ServeFn = std::function<void(const QByteArray&, const QUrl&, const QByteArray&,
                                   MobileWebBridge::Respond)>;

NSString* toNs(const QString& s)
{
    return [NSString stringWithUTF8String:s.toUtf8().constData()];
}

QString fromNs(NSString* s)
{
    return s ? QString::fromUtf8([s UTF8String]) : QString();
}

// THE WINDOW A MODULE'S PAGE LIVES IN, or nil when this app has none yet.
//
// QT'S OWN WINDOW FIRST, and that is what works. A Qt for iOS app has no scene
// manifest in its Info.plist, so `connectedScenes` is empty; and it does not put
// its UIWindow in `UIApplication.windows` early enough for a page opened before
// the event loop runs. Both spellings found nothing on the simulator and every
// page reported "no window to live in" -- which would have thrown it out of the
// hierarchy and throttled its requestAnimationFrame, so a QML runtime in it
// would have frozen.
//
// QWindow::winId() IS the QUIView, which knows its window whatever UIKit is
// willing to enumerate. The two scans after it stay as fallbacks for a host
// whose surface is not a QWindow at all.
UIWindow* hostWindow()
{
    for (QWindow* qtWindow : QGuiApplication::topLevelWindows()) {
        // winId() rather than handle(): it CREATES the platform window if there
        // is not one yet, which for a page opened before the event loop has
        // turned is the difference between finding Qt's UIWindow and not.
        UIView* qtView = (__bridge UIView*)reinterpret_cast<void*>(qtWindow->winId());
        if (qtView && qtView.window) return qtView.window;
    }

    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        NSArray<UIWindow*>* windows = ((UIWindowScene*)scene).windows;
        for (UIWindow* candidate in windows) {
            if (candidate.isKeyWindow) return candidate;
        }
        if (windows.firstObject) return windows.firstObject;
    }

    for (UIWindow* candidate in UIApplication.sharedApplication.windows) {
        if (candidate.isKeyWindow) return candidate;
    }
    return UIApplication.sharedApplication.windows.firstObject;
}

} // namespace

// The scheme handler. One per webview, holding that page's `serve`.
//
// A STOPPED TASK MUST NOT BE TOUCHED. WebKit calls stopURLSchemeTask when the
// page navigates away or the webview is torn down, and completing a task after
// that is an immediate crash with no diagnostic — which for a long poll parked
// for twenty seconds is the normal case, not an edge. So every live task is
// held here and a completion that does not find its task does nothing.
@interface LogosSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation LogosSchemeHandler {
    std::shared_ptr<ServeFn> _serve;
    NSMutableSet* _live;
}

- (instancetype)initWithServe:(std::shared_ptr<ServeFn>)serve
{
    if ((self = [super init])) {
        _serve = std::move(serve);
        _live = [NSMutableSet set];
    }
    return self;
}

- (void)webView:(WKWebView*)webView startURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)webView;
    [_live addObject:task];

    NSURLRequest* request = task.request;
    const QUrl url(fromNs(request.URL.absoluteString));
    const QByteArray method = fromNs(request.HTTPMethod).toUtf8();
    // NIL IN PRACTICE, and that is why the frame travels in the query: WebKit
    // hands a scheme handler a request whose HTTPBody and HTTPBodyStream are
    // both empty for a `fetch` with a body. Read anyway, so that the day WebKit
    // fixes it the shorter path simply starts working.
    QByteArray body;
    if (request.HTTPBody) {
        body = QByteArray(static_cast<const char*>(request.HTTPBody.bytes),
                          int(request.HTTPBody.length));
    }

    __weak LogosSchemeHandler* weakSelf = self;
    ServeFn serve = *_serve;
    if (!serve) {
        [_live removeObject:task];
        [task didFailWithError:[NSError errorWithDomain:@"co.logos.web" code:500 userInfo:nil]];
        return;
    }

    serve(method, url, body, [weakSelf, task, url](const BridgeReply& reply) {
        // COPIED FIRST. `reply` is a reference into the bridge's stack and this
        // runs later, on another queue.
        auto held = std::make_shared<BridgeReply>(reply);
        auto heldUrl = std::make_shared<QUrl>(url);
        // ALWAYS BACK TO THE MAIN QUEUE. A parked poll is answered from whatever
        // thread the core sent on, and a WKURLSchemeTask may only be completed
        // on the thread that started it.
        dispatch_async(dispatch_get_main_queue(), ^{
            LogosSchemeHandler* handler = weakSelf;
            if (!handler) return;
            [handler complete:task with:*held url:*heldUrl];
        });
    });
}

- (void)complete:(id<WKURLSchemeTask>)task with:(const BridgeReply&)reply url:(const QUrl&)url
{
    if (![_live containsObject:task]) return;   // stopped while we were away
    [_live removeObject:task];

    NSData* data = nil;
    if (reply.isFile()) {
        // MAPPED, not read: the largest thing served is the app's 26 MB QML
        // runtime image, and a copy of it on the heap on every module's first
        // load is the difference between fitting on a phone and not.
        data = [NSData dataWithContentsOfFile:toNs(reply.filePath)
                                      options:NSDataReadingMappedIfSafe
                                        error:nil];
        if (!data) {
            [task didFailWithError:[NSError errorWithDomain:@"co.logos.web"
                                                       code:404 userInfo:nil]];
            return;
        }
    } else {
        data = [NSData dataWithBytes:reply.body.constData() length:NSUInteger(reply.body.size())];
    }

    NSDictionary* headers = @{
        @"Content-Type" : toNs(QString::fromUtf8(reply.mimeType)),
        @"Content-Length" : [NSString stringWithFormat:@"%lu", (unsigned long)data.length],
        // The page fetches its own documents; without this the fetch is blocked
        // as a cross-origin request and the loader reports nothing useful.
        @"Access-Control-Allow-Origin" : @"*",
        @"Cache-Control" : @"no-store",
    };
    NSURL* nsUrl = [NSURL URLWithString:toNs(url.toString())];
    NSHTTPURLResponse* response = [[NSHTTPURLResponse alloc] initWithURL:nsUrl
                                                             statusCode:reply.status
                                                            HTTPVersion:@"HTTP/1.1"
                                                           headerFields:headers];
    [task didReceiveResponse:response];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)webView:(WKWebView*)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task
{
    (void)webView;
    [_live removeObject:task];
}

@end

// The navigation delegate, for the one fact WebKit alone has: WebContent died.
@interface LogosWebViewDelegate : NSObject <WKNavigationDelegate>
@end

@implementation LogosWebViewDelegate {
    std::shared_ptr<std::function<void()>> _onDied;
    QString _module;
}

- (instancetype)initWithModule:(QString)module died:(std::shared_ptr<std::function<void()>>)onDied
{
    if ((self = [super init])) {
        _onDied = std::move(onDied);
        _module = std::move(module);
    }
    return self;
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView
{
    (void)webView;
    qWarning() << "Web module" << _module << "lost its WebContent process";
    if (_onDied && *_onDied) (*_onDied)();
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error
{
    (void)webView; (void)navigation;
    qWarning() << "Web module" << _module << "could not load its entry document:"
               << fromNs(error.localizedDescription);
}

- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation
{
    (void)webView; (void)navigation;
    qInfo() << "Web module" << _module << "loaded its entry document";
}

@end

namespace basecamp::web {

QString iosQmlRuntimeDir()
{
    // Where the app bundle carries it. The rest of the question -- the
    // override, what counts as a runtime -- is the same on both phones.
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    return bundledQmlRuntimeDir(
        QDir(fromNs(resources)).filePath(QStringLiteral("logos-runtime")));
}

QString iosWebModulesDir()
{
    // NO UNPACKING STEP, unlike Android: an iOS app's resources ARE files on
    // disk, read-only, which is all the core's discovery wants. So the app's
    // shipped `web` modules are added to the core as a second modules
    // directory exactly where the build put them.
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    const QString dir = QDir(fromNs(resources)).filePath(QStringLiteral("web-modules"));
    return QFileInfo(dir).isDir() ? QDir(dir).absolutePath() : QString();
}

PlatformPageFactory iosPlatformPageFactory()
{
    return [](const PlatformPageRequest& request) -> PlatformPage {
        auto serve = std::make_shared<ServeFn>(request.serve);
        auto onDied = std::make_shared<std::function<void()>>(request.onDied);

        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        LogosSchemeHandler* handler = [[LogosSchemeHandler alloc] initWithServe:serve];
        [config setURLSchemeHandler:handler
                        forURLScheme:toNs(QString::fromLatin1(kSchemeName))];

        // AT DOCUMENT START, so the page's own first module script can read
        // `window.logosQmlRuntimeBase` and await `window.logosChannelReady`.
        WKUserScript* shim =
            [[WKUserScript alloc] initWithSource:toNs(request.channelShim)
                                   injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                forMainFrameOnly:YES];
        [config.userContentController addUserScript:shim];

        // A `web` variant instantiates two wasm images and draws through WebGL,
        // and a phone webview will not start a media-ish load on its own.
        config.allowsInlineMediaPlayback = YES;
        config.suppressesIncrementalRendering = NO;

        WKWebView* webView = [[WKWebView alloc] initWithFrame:CGRectZero configuration:config];
        LogosWebViewDelegate* delegate =
            [[LogosWebViewDelegate alloc] initWithModule:request.moduleName died:onDied];
        webView.navigationDelegate = delegate;
        webView.opaque = NO;
        webView.scrollView.bounces = NO;
        if (@available(iOS 16.4, *)) webView.inspectable = YES;

        // IN THE HIERARCHY, BUT AT THE BACK. A WKWebView outside a window is
        // throttled — requestAnimationFrame stops, and the Qt-wasm runtime
        // draws through it — so a page that was never mounted would come up and
        // then freeze. It goes behind Qt's own view at the window's size; the
        // shell brings it forward through the handle below when the user is
        // looking at this module.
        UIWindow* window = hostWindow();
        if (window) {
            webView.frame = window.bounds;
            webView.autoresizingMask =
                UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [window addSubview:webView];
            [window sendSubviewToBack:webView];
        } else {
            // MEASURED, and it is a TIMING fact rather than a failure: a page
            // opened before the event loop has turned (the bring-up probe does
            // exactly that) finds no UIWindow by any of hostWindow()'s routes.
            // The channel works regardless -- WKWebView runs its JavaScript
            // detached -- but requestAnimationFrame is throttled, so a QML
            // runtime in such a page would freeze. Modules are loaded from the
            // event loop and are unaffected.
            qWarning() << "Web module" << request.moduleName
                       << "found no window to live in; its page will run detached "
                          "and anything drawing through requestAnimationFrame "
                          "will be throttled";
        }

        NSURL* url = [NSURL URLWithString:toNs(request.entryUrl.toString())];
        [webView loadRequest:[NSURLRequest requestWithURL:url]];

        // ONE HOLDER, SHARED BY THE THREE CALLBACKS. WKWebView keeps its
        // navigationDelegate WEAKLY and nothing keeps the scheme handler, so
        // both have to be owned here — and destroy() and isAlive() have to see
        // the SAME webview pointer, which three separate lambda captures would
        // not.
        struct Holder {
            WKWebView* view = nil;
            LogosSchemeHandler* handler = nil;
            LogosWebViewDelegate* delegate = nil;
        };
        auto holder = std::make_shared<Holder>();
        holder->view = webView;
        holder->handler = handler;
        holder->delegate = delegate;

        PlatformPage page;
        page.destroy = [holder, serve] {
            if (!holder->view) return;
            *serve = nullptr;                 // no task started from here on
            [holder->view stopLoading];
            holder->view.navigationDelegate = nil;
            [holder->view removeFromSuperview];
            holder->view = nil;
            holder->handler = nil;
            holder->delegate = nil;
        };
        page.nativeHandle = [holder]() -> void* { return (__bridge void*)holder->view; };
        // THE Z-ORDER, and nothing else. The page is already mounted at the
        // window's size (above); what "the user is looking at this module"
        // means on iOS is which subview is in front of Qt's own QUIView.
        // Sending it back rather than hiding it is deliberate: a hidden
        // WKWebView is throttled, and a background module that is still
        // answering calls must keep its timers.
        page.setFrontmost = [holder](bool front) {
            UIView* view = holder->view;
            UIView* superview = view.superview;
            if (!view || !superview) return;
            if (front) [superview bringSubviewToFront:view];
            else [superview sendSubviewToBack:view];
        };
        // WHERE THE SHELL LEFT ROOM FOR IT (#110). A page mounted at the
        // window's size covers the Shell's own navigation the moment it comes
        // forward, and the user is then inside an app with no way out. The
        // Shell docks a placeholder for a web app and publishes the rect its
        // workspace gave it; this is that rect becoming a frame.
        //
        // UIKIT POINTS ARE QT'S LOGICAL PIXELS ON iOS -- the scale factor lives
        // in the window's contentScaleFactor, not in its bounds -- so the rect
        // crosses unconverted. The autoresizing mask goes with it: a frame the
        // host is driving must not also be driven by the superview's resizing,
        // or a rotation would stretch the page back over the chrome. An empty
        // rect gives the window back and restores the mask.
        page.setGeometry = [holder](const QRect& windowRect) {
            UIView* view = holder->view;
            UIView* superview = view.superview;
            if (!view || !superview) return;
            if (windowRect.isEmpty()) {
                view.autoresizingMask =
                    UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
                view.frame = superview.bounds;
                return;
            }
            view.autoresizingMask = UIViewAutoresizingNone;
            view.frame = CGRectMake(windowRect.x(), windowRect.y(),
                                    windowRect.width(), windowRect.height());
        };
        page.isAlive = [holder]() -> bool { return holder->view != nil; };
        // ONE SCRIPT IN THE PAGE, and what it says it says through the page's
        // own console -- which the bridge already carries back. On the main
        // queue because WKWebView is main-thread-only and a host may ask from
        // wherever it is driving.
        //
        // completionHandler:nil, AND IT IS NOT TIDINESS. A completion handler
        // makes WebKit deserialize the script's result, which builds a
        // JSVirtualMachine on the main thread -- and that traps in
        // JSC::sanitizeStackForVM under Qt's separate main stack. MEASURED: an
        // iPhone 16 Pro simulator, SIGTRAP on the first script, 2026-09-12.
        // Same trap, same cause, as the WKScriptMessageHandler finding that put
        // the channel on a URL scheme in the first place (MobileWebBridge.h);
        // this is the second thing in WebKit that cannot be used from a Qt iOS
        // app, and the answer is the same -- do not ask WebKit for a value.
        page.evaluateJavaScript = [holder](const QString& script) {
            NSString* source = toNs(script);
            dispatch_async(dispatch_get_main_queue(), ^{
                WKWebView* view = holder->view;
                if (!view) return;
                [view evaluateJavaScript:source completionHandler:nil];
            });
        };
        return page;
    };
}

} // namespace basecamp::web
