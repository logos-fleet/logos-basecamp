#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <deque>
#include <functional>
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
//   POST logos://module/__logos/<token>/send    one frame, page -> host
//   GET  logos://module/__logos/<token>/poll    frames, host -> page (long poll)
//   POST logos://module/__logos/<token>/close   the page has stopped serving
//
// Android's `shouldInterceptRequest` is the same seam by nature, so one bridge
// serves both phones and the platform halves are the twenty lines that turn a
// WKURLSchemeTask or a WebResourceRequest into a handleRequest() call.
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
    // the builder emits.
    MobileWebBridge(QString moduleDir, QString runtimeDir, QString entryFile);
    ~MobileWebBridge();

    // The URL the webview is asked to load. Never a file: URL.
    QUrl entryUrl() const;

    // This launch's token. Fresh per bridge, so one module's page cannot use
    // another's control paths even inside one app run.
    QString launchToken() const { return m_token; }

    // The script injected at document start, ahead of the page's own scripts.
    // It publishes `window.logosQmlRuntimeBase` and `window.logosChannelReady`
    // — the same two names the desktop container publishes, because the loader
    // that reads them is one shipped file and does not know which container it
    // is in.
    QString channelShim() const;

    // One request from the page. `method` is "GET" or "POST".
    void handleRequest(const QByteArray& method, const QUrl& url,
                       const QByteArray& body, Respond respond);

    // ── the host's end of the channel ──────────────────────────────────────
    void setReceiver(Receiver receiver);
    bool send(const std::string& frame);
    void close();
    bool isOpen() const;

    // The page said it has stopped serving (its wasm image trapped, its loader
    // gave up). The container's verdict for this is the same as for a page that
    // died, because it is the same fact.
    void setOnPageClosed(std::function<void()> callback);

    // Answer every parked poll with an empty batch. The platform half calls
    // this on a timer: a long poll the host never completes is a fetch the page
    // waits on forever, and the page re-arms as soon as one settles.
    void expireWaits();

    // How long a poll may be parked before expireWaits() should answer it.
    // Shorter than any webview's own request timeout, and long enough that an
    // idle module is not spinning through a fetch a second.
    static constexpr int kPollParkMs = 20000;

private:
    // The reply to a poll, built from `m_outbound` with the lock held.
    BridgeReply drainLocked();
    static BridgeReply closedReply();
    static BridgeReply framesReply(const std::vector<std::string>& frames);

    const QString m_moduleDir;
    const QString m_runtimeDir;
    const QString m_entryFile;
    const QString m_token;

    mutable std::mutex m_mutex;
    bool m_open = true;
    std::deque<std::string> m_outbound;
    // Polls the page has in flight with nothing to give them yet. More than one
    // is not expected and is not an error: a page that re-armed before its
    // previous fetch settled would have two, and dropping either would strand a
    // request the webview is still waiting on.
    std::vector<Respond> m_waiting;

    // Guarded by its own mutex rather than m_mutex: a receiver may detach
    // itself from inside a delivery, and IMessageChannel's contract says
    // setReceiver(nullptr) must not return while one is still running.
    mutable std::recursive_mutex m_receiverMutex;
    Receiver m_receiver;

    std::function<void()> m_onPageClosed;
};

} // namespace basecamp::web
