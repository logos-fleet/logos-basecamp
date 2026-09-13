// THE PHONE BRIDGE, WITH A REAL BROWSER ON THE OTHER END.
//
// tests/mobile_web_bridge_test.cpp drives MobileWebBridge from C++: requests in,
// replies out. That covers everything the bridge decides and nothing the PAGE
// does — and the page's half is a JavaScript shim the bridge itself ships, with
// a long-poll loop, a chunked sender and a console wrapper in it, which no
// desktop test otherwise executes at all.
//
// So this runs it. A QWebEngineUrlSchemeHandler forwards every `logos:` request
// straight into bridge->handleRequest, the shim is injected at document
// creation, and a fixture page uses `window.logosChannelReady` exactly as a
// `web` variant's loader does. That is the SAME twenty lines of adapter each
// phone writes (IosWebPage.mm is the iOS one), so what passes here is what a
// device runs.
//
// WHAT IT CANNOT PROVE is the platform half: that WKWebView delivers a request
// to a scheme handler at all, that a stopped task must not be touched, that
// Android's asset loader behaves. Those are device facts. What it proves is
// everything between the two, which is where the protocol lives.
//
// Run: nix build .#mobile-bridge-test -L

#include "webview/MobileWebBridge.h"
#include "web/LogosWebPaths.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>

#include <algorithm>
#include <cstdio>
#include <vector>

using basecamp::web::BridgeReply;
using basecamp::web::MobileWebBridge;

namespace {

struct Check {
    QString name;
    bool ok = false;
    QString detail;
};
std::vector<Check> gChecks;

void check(const QString& name, bool ok, const QString& detail = {})
{
    gChecks.push_back({ name, ok, detail });
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData(),
           detail.isEmpty() ? "" : "  -- ", detail.toUtf8().constData());
    fflush(stdout);
}

// Pump the event loop. Everything waits this way rather than sleeping: the page
// delivers on this thread, so a sleeping test prevents the thing it waits for.
void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

template <typename Predicate>
bool waitFor(Predicate done, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (done()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

// THE ADAPTER — the whole of what a platform contributes, on the desktop's
// browser. IosWebPage.mm's startURLSchemeTask is this, in Objective-C, plus a
// hop to the main queue that Chromium does not need because a QWebEngine job is
// already answered on this thread.
class ForwardingSchemeHandler : public QWebEngineUrlSchemeHandler {
public:
    explicit ForwardingSchemeHandler(MobileWebBridge* bridge, QObject* parent = nullptr)
        : QWebEngineUrlSchemeHandler(parent), m_bridge(bridge) {}

    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        const QByteArray method = job->requestMethod();
        const QUrl url = job->requestUrl();
        QByteArray body;
        if (QIODevice* device = job->requestBody()) body = device->readAll();

        QPointer<QWebEngineUrlRequestJob> alive(job);
        m_bridge->handleRequest(method, url, body, [alive, url](const BridgeReply& reply) {
            // A LONG POLL IS ANSWERED LATE, and by then the page may have
            // navigated away and Chromium destroyed the job. Same rule, same
            // reason, as the live-task set in IosWebPage.mm.
            if (!alive) return;
            if (reply.status >= 400) {
                alive->fail(QWebEngineUrlRequestJob::UrlNotFound);
                return;
            }
            QByteArray bytes = reply.body;
            if (reply.isFile()) {
                QFile file(reply.filePath);
                if (!file.open(QIODevice::ReadOnly)) {
                    alive->fail(QWebEngineUrlRequestJob::RequestFailed);
                    return;
                }
                bytes = file.readAll();
            }
            auto* buffer = new QBuffer(alive);
            buffer->setData(bytes);
            buffer->open(QIODevice::ReadOnly);
            alive->reply(reply.mimeType, buffer);
        });
    }

private:
    MobileWebBridge* m_bridge;
};

// A `web` variant's loader, reduced to what it asks of the container: await the
// channel, install a receiver, send. Nothing here is Qt-wasm — this fixture is
// about the CHANNEL, and a 26 MB runtime would only make its failures slower.
const char* kFixturePage = R"HTML(
<!doctype html>
<html><head><meta charset="utf-8"><title>fixture</title></head>
<body>
<script type="module">
const channel = await window.logosChannelReady;
window.__received = [];
channel.setReceiver((text) => {
  window.__received.push(text);
  if (text === 'ping') channel.send('pong');
});
console.log('fixture is listening; runtime base ' + window.logosQmlRuntimeBase);
window.__channel = channel;
window.__ready = true;
</script>
</body></html>
)HTML";

} // namespace

int main(int argc, char* argv[])
{
    QWebEngineUrlScheme scheme(basecamp::web::kSchemeName);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    QApplication app(argc, argv);

    QTemporaryDir root;
    if (!root.isValid()) { fprintf(stderr, "no temp dir\n"); return 2; }
    const QString moduleDir = QDir(root.path()).filePath("counter_ui");
    const QString runtimeDir = QDir(root.path()).filePath("logos-runtime");
    QDir().mkpath(moduleDir);
    QDir().mkpath(runtimeDir);
    {
        QFile page(QDir(moduleDir).filePath("index.html"));
        page.open(QIODevice::WriteOnly);
        page.write(kFixturePage);
    }
    {
        QFile glue(QDir(runtimeDir).filePath("logos_qml_runtime.js"));
        glue.open(QIODevice::WriteOnly);
        glue.write("// the bundled runtime's glue, as far as this fixture cares\n");
    }

    MobileWebBridge bridge(moduleDir, runtimeDir, QStringLiteral("index.html"));

    std::vector<std::string> fromPage;
    bridge.setReceiver([&fromPage](const std::string& text) { fromPage.push_back(text); });
    QStringList console;
    bridge.setOnPageLog([&console](const QString& level, const QString& message) {
        console.append(level + ": " + message);
        printf("      [page %s] %s\n", level.toUtf8().constData(),
               message.toUtf8().constData());
        fflush(stdout);
    });
    bool pageClosed = false;
    bridge.setOnPageClosed([&pageClosed] { pageClosed = true; });

    auto* profile = new QWebEngineProfile(&app);
    profile->installUrlSchemeHandler(QByteArray(basecamp::web::kSchemeName),
                                    new ForwardingSchemeHandler(&bridge, profile));

    QWebEngineView view;
    auto* page = new QWebEnginePage(profile, &view);
    view.setPage(page);

    QWebEngineScript shim;
    shim.setName(QStringLiteral("logos-mobile-channel"));
    shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shim.setWorldId(QWebEngineScript::MainWorld);
    shim.setRunsOnSubFrames(false);
    shim.setSourceCode(bridge.channelShim());
    page->scripts().insert(shim);

    // The poll loop parks for twenty seconds at a time; a test that waited that
    // long for each empty batch would be a test nobody runs. The container's own
    // timer does exactly this on a phone.
    QTimer expiry;
    expiry.setInterval(500);
    QObject::connect(&expiry, &QTimer::timeout, [&bridge] { bridge.expireWaits(); });
    expiry.start();

    view.resize(320, 240);
    view.load(bridge.entryUrl());

    const bool ready = waitFor([&console] {
        return console.join(' ').contains("fixture is listening");
    });
    check("the page loads off the logos: scheme and the shim publishes a channel", ready,
          ready ? QString() : QStringLiteral("console: ") + console.join(" | "));
    if (!ready) {
        printf("\nThe page never came up. Nothing below can mean anything.\n");
        return 1;
    }

    // The console line the fixture printed carries the value the container put
    // in the page, which is what a `ui_qml` variant's loader reads to find the
    // bundled runtime.
    check("the page sees the bundled runtime's base URL",
          console.join(' ').contains("logos://module/logos-runtime/"),
          console.join(" | "));

    // ── host -> page -> host ────────────────────────────────────────────────
    const bool sent = bridge.send("ping");
    check("the host can send while the page is idle on a long poll", sent);
    const bool answered = waitFor([&fromPage] {
        return !fromPage.empty() && fromPage.front() == "pong";
    });
    check("a frame reaches the page and the page's answer reaches the host", answered,
          answered ? QString()
                   : QStringLiteral("got %1 frame(s)").arg(fromPage.size()));

    // ── the chunked sender ──────────────────────────────────────────────────
    //
    // THE REASON THE SENDER IS CHUNKED AT ALL. Neither phone hands its
    // interceptor a request body, so a frame travels percent-encoded in the
    // query and is split at 4000 characters. A 30 KB frame is eight requests
    // that have to arrive as ONE string, in order.
    fromPage.clear();
    page->runJavaScript(QStringLiteral(
        "window.__big = 'z'.repeat(30000);"
        "window.__channel.send(JSON.stringify({ type: 'Result', payload: window.__big }));"));
    const bool chunked = waitFor([&fromPage] { return !fromPage.empty(); });
    check("a frame far larger than one request arrives whole", chunked,
          chunked ? QStringLiteral("%1 bytes").arg(fromPage.front().size())
                  : QStringLiteral("nothing arrived"));
    if (chunked) {
        // 30000 z's plus the JSON around them. Both halves matter: a truncated
        // frame is short, and an interleaved one is the right length with the
        // wrong bytes in the middle.
        const QString whole = QString::fromStdString(fromPage.front());
        check("...and is not truncated or interleaved",
              whole.size() > 30000 && whole.count(QLatin1Char('z')) == 30000
                  && whole.startsWith(QLatin1String("{\"type\":\"Result\""))
                  && whole.endsWith(QLatin1Char('}')),
              QStringLiteral("%1 chars, %2 z").arg(whole.size()).arg(whole.count(QLatin1Char('z'))));
        check("...and exactly one frame arrived, not one per chunk",
              fromPage.size() == 1, QStringLiteral("%1 frame(s)").arg(fromPage.size()));
    }

    // ── the chunked sender, with a frame that is not all letters ────────────
    //
    // THE FRAME THAT BROKE THE WALLET UI ON THE IPAD, and the one shape the
    // 30 KB case above cannot have. 30000 `z` characters percent-encode to
    // themselves, so a boundary drawn every 4000 characters of the ENCODED text
    // can never land inside an escape. A real frame is JSON — every `{`, `"`,
    // `,` and `:` is three characters on the wire — and a contract query
    // answering a forty-method surface was the first one in this tree big
    // enough to split at all. Cut `%22` after its `%` and the two halves decode
    // to two different strings: the host delivers something that is no longer
    // JSON, the peer drops it without a word, and the container reports "the
    // page never published a module" about a page that answered perfectly.
    fromPage.clear();
    QString dense = QStringLiteral("{\"type\":\"Result\",\"methods\":[");
    for (int i = 0; i < 400; ++i) {
        if (i) dense += QLatin1Char(',');
        dense += QStringLiteral("{\"name\":\"method_%1\",\"sig\":\"QString(QString,int)\"}").arg(i);
    }
    dense += QStringLiteral("]}");
    page->runJavaScript(QStringLiteral(
        "window.__dense = '{\"type\":\"Result\",\"methods\":[' + Array.from({length:400},"
        "  function (_, i) { return '{\"name\":\"method_' + i"
        "    + '\",\"sig\":\"QString(QString,int)\"}'; }).join(',') + ']}';"
        "window.__channel.send(window.__dense);"));
    const bool denseArrived = waitFor([&fromPage] { return !fromPage.empty(); });
    check("an escape-dense frame larger than one request arrives", denseArrived,
          denseArrived ? QStringLiteral("%1 bytes").arg(fromPage.front().size())
                       : QStringLiteral("nothing arrived"));
    if (denseArrived) {
        const QString whole = QString::fromStdString(fromPage.front());
        check("...and it is the frame the page sent, byte for byte",
              whole == dense,
              whole == dense
                  ? QStringLiteral("%1 chars").arg(whole.size())
                  : QStringLiteral("expected %1 chars, got %2; first difference at %3")
                        .arg(dense.size()).arg(whole.size())
                        .arg([&] {
                            const int n = std::min(dense.size(), whole.size());
                            for (int i = 0; i < n; ++i)
                                if (dense.at(i) != whole.at(i)) return i;
                            return n;
                        }()));
    }

    // ── the page reporting that it has stopped ──────────────────────────────
    page->runJavaScript(QStringLiteral("window.__channel.close();"));
    const bool closed = waitFor([&pageClosed] { return pageClosed; }, 10000);
    check("a page that closes its channel is reported to the host", closed);
    check("...and the host's end is closed with it", !bridge.isOpen());

    pump(200);

    int failed = 0;
    for (const Check& c : gChecks) if (!c.ok) ++failed;
    printf("\n%zu checks, %d failed\n", gChecks.size(), failed);
    return failed == 0 ? 0 : 1;
}
