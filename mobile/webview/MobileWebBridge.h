#pragma once

#include "web/LogosWebPaths.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace basecamp::web {

// What one request from the page is answered with.
//
// A FILE IS NAMED RATHER THAN READ. The largest thing either phone container
// serves is the app's 26 MB QML runtime image, and both platforms can stream a
// file off disk into the page (a WKURLSchemeTask takes NSData in chunks, an
// Android WebResourceResponse takes an InputStream). A reply that carried the
// bytes would put that image in the app's heap on the way past — twice, on the
// first load of every module.
struct BridgeReply {
    int status = 200;
    QByteArray mimeType;
    QByteArray body;      // set when filePath is empty
    QString filePath;     // a file to stream, or empty

    bool isFile() const { return !filePath.isEmpty(); }
};

// A WEB MODULE'S PAGE ON A PHONE: what it may fetch, and how its frames cross.
//
// THE CHANNEL IS THE SCHEME, and that is a finding rather than a preference.
// The desktop container publishes a QWebChannel object into the page; neither
// phone has one, and the obvious substitute is ruled out on iOS — the mobile
// round-trip spike (2026-09-09) found that a WKScriptMessageHandler traps in
// JSC::sanitizeStackForVM when the page is entered under Qt's separate main
// stack. What is left is the custom URL scheme the loader already needs for its
// documents, so the channel lives on it, under a reserved prefix:
//
//   GET  logos://module/__logos/<token>/send?s&i&n&d   one frame, page -> host
//   GET  logos://module/__logos/<token>/poll           frames, host -> page
//   GET  logos://module/__logos/<token>/close          the page stopped serving
//   GET  logos://module/__logos/<token>/log?l&m        one console line
//
// Android's `shouldInterceptRequest` is the same seam by nature, so one bridge
// serves both phones and the platform halves are the twenty lines that turn a
// WKURLSchemeTask or a WebResourceRequest into a handleRequest() call.
//
// THE OUTGOING FRAME IS IN THE URL, AND CHUNKED, which looks wrong until you
// try the obvious thing. NEITHER phone hands a request body to its interceptor:
// WKURLSchemeHandler receives a request whose HTTPBody and HTTPBodyStream are
// both nil for a `fetch` POST, and Android's WebResourceRequest has no body
// accessor at all -- it exposes the method, the URL and the headers and nothing
// else. A frame is therefore percent-encoded into a query parameter, and split
// because a transport frame can carry bytes: `d` is one chunk of the encoded
// text, `s` is the frame's sequence number, `i` and `n` its index and count.
// The bridge reassembles and delivers when the last chunk lands.
//
// The POST-with-a-body spelling is accepted too, and is not dead code: it is
// what a container whose interceptor DOES see bodies would use (the desktop's
// would, if it ever dropped QWebChannel), and it is the shortest way for a test
// to hand the bridge one frame.
//
// NO PORT IS BOUND. The whole exchange is intra-webview; nothing here listens
// on a socket, which is the fourth acceptance criterion of slice 28 discharged
// by construction rather than by policy. The per-launch token is kept anyway
// and checked on every control request: it costs nothing, it is what the same
// criterion demands of the loopback path if that is ever chosen instead, and it
// means a document the page was tricked into loading cannot speak as the module
// just by knowing the scheme.
//
// THE HOST-TO-PAGE DIRECTION IS BUFFERED, and that is not an optimisation. The
// container starts talking the moment it has published the module — the first
// thing it sends is the wildcard event SUBSCRIBE — while the page is still
// bringing up a 26 MB runtime and has not polled yet. A bridge that answered an
// empty batch and forgot would lose it, and the module would serve calls and no
// events. Same finding, same fix, as the desktop PageBridge's backlog.
//
// THREADING: handleRequest() arrives on the platform's UI thread; send() and
// close() arrive on whatever thread the core is using. A parked poll is
// therefore answered from the SENDER's thread, so a platform half must hop to
// its UI thread inside the Respond callback it hands in (a WKURLSchemeTask may
// only be completed on the thread that started it).
class MobileWebBridge {
public:
    // Called exactly once per request, now or later.
    using Respond = std::function<void(const BridgeReply&)>;
    using Receiver = std::function<void(const std::string&)>;

    // `entryFile` is the module's entry document relative to `moduleDir` — the
    // package manifest's `main`, which is an .html file for every web variant
    // the builder emits. `origin` is what the page is served on and defaults to
    // `logos://module`; Android needs WebOrigin::android() (see LogosWebPaths.h,
    // where the measurement is).
    MobileWebBridge(QString moduleDir, QString runtimeDir, QString entryFile,
                    WebOrigin origin = {});
    ~MobileWebBridge();

    // The URL the webview is asked to load. Never a file: URL.
    QUrl entryUrl() const;

    // The URL of ANY document in this module's package, on the origin this
    // bridge serves it on. A `web` variant has two entry documents -- the UI
    // page and the headless one a background module is swapped onto -- and the
    // container names the second, so the bridge answers for both rather than
    // holding one of them as state.
    QUrl documentUrl(const QString& file) const;

    // This launch's token. Fresh per bridge, so one module's page cannot use
    // another's control paths even inside one app run.
    QString launchToken() const { return m_token; }

    // The script injected at document start, ahead of the page's own scripts.
    // It publishes `window.logosQmlRuntimeBase` and `window.logosChannelReady`
    // — the same two names the desktop container publishes, because the loader
    // that reads them is one shipped file and does not know which container it
    // is in.
    QString channelShim() const;

    // SERVE THE SHIM INSIDE THE HTML instead of expecting the platform to
    // inject it.
    //
    // iOS has WKUserScript and an injection time of "document start", which is
    // the right seam: the script runs before the page's own and the document on
    // disk is untouched. ANDROID HAS NEITHER. `WebView.evaluateJavascript` runs
    // after the document has already started executing, which is too late for a
    // loader whose first module script reads `window.logosQmlRuntimeBase`, and
    // there is no user-script API to run one earlier. The one seam Android does
    // have is the interceptor this class already is -- so on Android the
    // container serves an entry document with the shim in its <head>.
    //
    // Off by default, because the platform that can do it properly should.
    void setInjectsShimIntoHtml(bool injects);

    // One request from the page. `method` is "GET" or "POST".
    void handleRequest(const QByteArray& method, const QUrl& url,
                       const QByteArray& body, Respond respond);

    // ── the host's end of the channel ──────────────────────────────────────
    void setReceiver(Receiver receiver);
    bool send(const std::string& frame);
    void close();
    bool isOpen() const;

    // EVERY CONSOLE LINE THE PAGE WRITES. A page whose script throws is the
    // hardest failure to diagnose from outside: the container sees "the page
    // never published a module" and nothing else, because a page that died on
    // line one is indistinguishable from one that simply has no module in it.
    // On the desktop QWebEnginePage hands the console over; here the shim
    // forwards it down the same scheme, so both containers put it in the app's
    // log.
    void setOnPageLog(std::function<void(const QString& level,
                                         const QString& message)> callback);

    // EVERY FRAME THE PAGE SENDS, in ADDITION to the receiver.
    //
    // The receiver belongs to the core's protocol client and there is exactly
    // one of it. This is for a host that has to SHOW what the module answered
    // — on a phone, that a module whose UI has been evicted is still answering
    // calls — which it cannot do by taking the receiver away without breaking
    // the routing it is trying to observe. Off by default and nothing in the
    // container sets it.
    void setFrameObserver(std::function<void(const QString& frame)> observer);

    // The page said it has stopped serving (its wasm image trapped, its loader
    // gave up). The container's verdict for this is the same as for a page that
    // died, because it is the same fact.
    void setOnPageClosed(std::function<void()> callback);

    // Answer every parked poll with an empty batch. The platform half calls
    // this on a timer: a long poll the host never completes is a fetch the page
    // waits on forever, and the page re-arms as soon as one settles.
    void expireWaits();

    // How much percent-encoded frame text one request may carry. Comfortably
    // inside every webview's URL limit, and large enough that an ordinary Call
    // is one request.
    static constexpr int kChunkChars = 4000;

    // How long a poll may be parked before expireWaits() should answer it.
    // Shorter than any webview's own request timeout, and long enough that an
    // idle module is not spinning through a fetch a second.
    static constexpr int kPollParkMs = 20000;

private:
    // One frame's chunks, until the last one lands. Keyed by the page's own
    // sequence number, so two frames in flight cannot interleave.
    struct Partial {
        int count = 0;
        std::vector<QString> chunks;
        int have = 0;
    };

    // `html` with the shim inserted as early as the document allows.
    QByteArray withShim(const QByteArray& html) const;

    // One request each, once handleRequest() has decided which it is. Every one
    // of them calls `respond` exactly once, except handlePoll(), which may park
    // it instead — see m_waiting.
    void serveDocument(const QString& path, const Respond& respond);
    void handleSend(const QUrl& url, const QByteArray& body, const Respond& respond);
    void handlePoll(Respond respond);
    void handleLog(const QUrl& url, const Respond& respond);
    void handleClose(const Respond& respond);

    // The reply to a poll, built from `m_outbound` with the lock held.
    BridgeReply drainLocked();
    static BridgeReply closedReply();
    static BridgeReply framesReply(const std::vector<std::string>& frames);

    const QString m_moduleDir;
    const QString m_runtimeDir;
    const QString m_entryFile;
    const WebOrigin m_origin;
    const QString m_token;
    bool m_injectShim = false;

    mutable std::mutex m_mutex;
    bool m_open = true;
    std::deque<std::string> m_outbound;
    // Polls the page has in flight with nothing to give them yet. More than one
    // is not expected and is not an error: a page that re-armed before its
    // previous fetch settled would have two, and dropping either would strand a
    // request the webview is still waiting on.
    std::vector<Respond> m_waiting;
    std::map<QString, Partial> m_partial;

    // Guarded by its own mutex rather than m_mutex: a receiver may detach
    // itself from inside a delivery, and IMessageChannel's contract says
    // setReceiver(nullptr) must not return while one is still running.
    mutable std::recursive_mutex m_receiverMutex;
    Receiver m_receiver;
    std::function<void(const QString&)> m_observer;

    std::function<void()> m_onPageClosed;
    std::function<void(const QString&, const QString&)> m_onPageLog;
};

} // namespace basecamp::web
