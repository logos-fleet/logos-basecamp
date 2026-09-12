#include "webview/WebPageProbe.h"

#include "webview/MobileWebBridge.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>

#include <utility>

namespace basecamp::web {

namespace {

// What a `web` variant's loader asks of the container, and nothing else: await
// the channel, install a receiver, answer. The same fixture the desktop browser
// check uses, so a failure here and a pass there is a PLATFORM finding.
const char* kFixture = R"HTML(
<!doctype html>
<html><head><meta charset="utf-8"><title>probe</title></head>
<body>
<script type="module">
const channel = await window.logosChannelReady;
channel.setReceiver((text) => {
  if (text === 'probe-ping') channel.send('probe-pong:' + window.logosQmlRuntimeBase);
});
console.log('probe page is listening');
</script>
</body></html>
)HTML";

bool pumpUntil(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (done()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

} // namespace

WebPageProbe::WebPageProbe(PlatformPageFactory platform, bool shimInDocument,
                           WebOrigin origin, QObject* parent)
    : QObject(parent)
    , m_platform(std::move(platform))
    , m_shimInDocument(shimInDocument)
    , m_origin(std::move(origin))
{
}

QString WebPageProbe::writeFixture()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = QDir(base).filePath(QStringLiteral("web-probe"));
    if (!QDir().mkpath(dir)) return {};
    QFile page(QDir(dir).filePath(QStringLiteral("index.html")));
    if (!page.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    page.write(kFixture);
    page.close();
    return dir;
}

bool WebPageProbe::run(int timeoutMs)
{
    if (!m_platform) {
        emit log(QStringLiteral("web probe: this build has no webview backend"));
        return false;
    }

    const QString dir = writeFixture();
    if (dir.isEmpty()) {
        emit log(QStringLiteral("web probe: could not write the fixture page"));
        return false;
    }

    // No runtime directory: the fixture loads no wasm, and an empty one is also
    // what an app that shipped without a runtime has — so the value the page
    // reports back says which of the two this build is.
    MobileWebBridge bridge(dir, QString(), QStringLiteral("index.html"), m_origin);
    bridge.setInjectsShimIntoHtml(m_shimInDocument);

    // A MEMBER rather than a local, because the queued delivery below may be
    // posted while this function is still running and run on the main thread
    // after it has returned; a captured reference to a local would dangle.
    m_console.clear();
    // ON THE QT MAIN THREAD, AND NOT WHERE THIS IS CALLED FROM. Android hands
    // `shouldInterceptRequest` to a Chromium background thread (see
    // LogosWebPage.java: that is what makes the long poll free), so a page's
    // console line arrives on it -- and `log` is connected DIRECTLY to a host
    // that appends to a QPlainTextEdit. Widgets from a second thread is an
    // immediate SIGSEGV inside QTextDocumentPrivate::insert; measured on a
    // Samsung SM-G990B, 2026-09-12, where it took the whole app down mid-probe.
    //
    // Queued, so the line is delivered by the processEvents() this probe is
    // already pumping -- and `console` is appended on that thread too, rather
    // than being a QStringList two threads touch.
    bridge.setOnPageLog([this](const QString& level, const QString& message) {
        QMetaObject::invokeMethod(this, [this, level, message] {
            m_console.append(message);
            emit log(QStringLiteral("web probe: page %1: %2").arg(level, message));
        }, Qt::QueuedConnection);
    });
    // The same hop, for the same reason: a frame from the page arrives on
    // whichever thread this platform serves its requests on, and the wait below
    // reads this list on the main one.
    m_fromPage.clear();
    bridge.setReceiver([this](const std::string& text) {
        const QString line = QString::fromStdString(text);
        QMetaObject::invokeMethod(this, [this, line] { m_fromPage.append(line); },
                                  Qt::QueuedConnection);
    });

    PlatformPageRequest request;
    request.moduleName = QStringLiteral("web_probe");
    request.entryUrl = bridge.entryUrl();
    request.channelShim = m_shimInDocument ? QString() : bridge.channelShim();
    request.serve = [&bridge](const QByteArray& method, const QUrl& url,
                              const QByteArray& body, MobileWebBridge::Respond respond) {
        bridge.handleRequest(method, url, body, std::move(respond));
    };
    request.onDied = [this] { emit log(QStringLiteral("web probe: the page died")); };

    QElapsedTimer since;
    since.start();
    PlatformPage page = m_platform(request);
    if (!page.destroy) {
        emit log(QStringLiteral("web probe: the platform opened no webview"));
        return false;
    }

    const bool listening = pumpUntil(
        [this] { return m_console.join(QLatin1Char(' ')).contains("probe page is listening"); },
        timeoutMs);
    emit log(listening
                 ? QStringLiteral("web probe: the page loaded off %1 and published a channel "
                                  "(%2 ms)").arg(bridge.entryUrl().toString())
                       .arg(since.elapsed())
                 : QStringLiteral("web probe: the page never published a channel"));
    if (!listening) { page.destroy(); return false; }

    bridge.send("probe-ping");
    const bool answered = pumpUntil([this] { return !m_fromPage.isEmpty(); }, timeoutMs);
    if (answered) {
        emit log(QStringLiteral("web probe: round trip OK -- %1 (%2 ms)")
                     .arg(m_fromPage.first()).arg(since.elapsed()));
    } else {
        emit log(QStringLiteral("web probe: the page never answered"));
    }

    page.destroy();
    return answered;
}

} // namespace basecamp::web
