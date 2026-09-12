// srcdeps: webview/MobileWebBridge.cpp web/LogosWebPaths.cpp
//
// THE PHONE CONTAINERS' HALF OF A WEB MODULE'S PAGE — what it may fetch, and
// how its frames cross (slice 28).
//
// On the desktop a page talks to the host over a QWebChannel. Neither phone has
// one, and neither may have the obvious substitute: the mobile round-trip spike
// found that a WKScriptMessageHandler TRAPS in JSC::sanitizeStackForVM under
// Qt's separate-main-stack entry, which rules out script messages on iOS
// altogether. What is left — and what the Qt-wasm loader already needs for its
// documents — is the custom URL scheme, so the channel is a pair of reserved
// paths on it: the page POSTs a frame to send one and long-polls to receive.
// Android's `shouldInterceptRequest` is the same seam by nature, so one bridge
// serves both.
//
// This tests that bridge with no webview at all: requests in, replies out. The
// platform halves (the WKURLSchemeHandler, the Android WebViewClient) are the
// twenty lines left over, and a phone is the worst place to find out that the
// frame queue drops events or that a package can escape its directory.
//
// Run: nix build .#unit-tests -L

#include "webview/MobileWebBridge.h"

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrlQuery>

#include <algorithm>

using basecamp::web::BridgeReply;
using basecamp::web::MobileWebBridge;

namespace {

QString write(const QDir& dir, const QString& relative, const QByteArray& bytes)
{
    const QString path = dir.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    f.close();
    return path;
}

} // namespace

class MobileWebBridgeTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QString m_moduleDir;
    QString m_runtimeDir;

    // The last reply `handleRequest` produced, or `pending` when it parked the
    // request instead of answering it.
    struct Call {
        bool answered = false;
        BridgeReply reply;
    };

    // Issue one request and collect whatever the bridge did with it.
    std::shared_ptr<Call> request(MobileWebBridge& bridge, const char* method,
                                  const QString& path, const QByteArray& body = {})
    {
        auto call = std::make_shared<Call>();
        QUrl url;
        url.setScheme(QStringLiteral("logos"));
        url.setHost(QStringLiteral("module"));
        url.setPath(path);
        bridge.handleRequest(method, url, body, [call](const BridgeReply& reply) {
            call->answered = true;
            call->reply = reply;
        });
        return call;
    }

    QString control(const MobileWebBridge& bridge, const char* leaf) const
    {
        return QStringLiteral("/__logos/%1/%2").arg(bridge.launchToken(), QLatin1String(leaf));
    }

    // What the shipped shim does: percent-encode the frame, split it, and GET
    // one request per chunk. Neither phone's interceptor is handed a request
    // body, so this -- not the POST below it -- is the path a device uses.
    void postChunked(MobileWebBridge& bridge, const QString& frame, int seq,
                     int chunkChars = MobileWebBridge::kChunkChars)
    {
        const QString encoded = QString::fromUtf8(
            QUrl::toPercentEncoding(frame, QByteArray(), QByteArray("/")));
        const int size = int(encoded.size());
        const int count = std::max(1, (size + chunkChars - 1) / chunkChars);
        for (int i = 0; i < count; ++i) {
            QUrl url;
            url.setScheme(QStringLiteral("logos"));
            url.setHost(QStringLiteral("module"));
            url.setPath(control(bridge, "send"));
            QUrlQuery query;
            query.addQueryItem("s", QString::number(seq));
            query.addQueryItem("i", QString::number(i));
            query.addQueryItem("n", QString::number(count));
            query.addQueryItem("d", encoded.mid(i * chunkChars, chunkChars));
            url.setQuery(query);
            bridge.handleRequest("GET", url, {}, [](const BridgeReply&) {});
        }
    }

    MobileWebBridge* make() const
    {
        return new MobileWebBridge(m_moduleDir, m_runtimeDir, QStringLiteral("index.html"));
    }

    static QStringList framesIn(const BridgeReply& reply)
    {
        QStringList out;
        const QJsonObject o = QJsonDocument::fromJson(reply.body).object();
        for (const QJsonValue& v : o.value("frames").toArray()) out.append(v.toString());
        return out;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QDir root(m_root.path());
        QDir().mkpath(root.filePath("modules/counter_ui"));
        QDir().mkpath(root.filePath("logos-runtime"));
        // CANONICAL, because resolveUnder is: /tmp and /var are symlinks on
        // macOS, so the path the bridge answers with is the resolved one and a
        // test comparing against the unresolved spelling fails for no reason.
        m_moduleDir = QFileInfo(root.filePath("modules/counter_ui")).canonicalFilePath();
        m_runtimeDir = QFileInfo(root.filePath("logos-runtime")).canonicalFilePath();
        write(QDir(m_moduleDir), "index.html", "<!doctype html><body></body>");
        write(QDir(m_moduleDir), "view/Main.qml", "import QtQuick 2.15\nItem {}\n");
        write(QDir(m_moduleDir), "counter_view_backend.wasm", QByteArray("\0asm", 4));
        write(QDir(m_runtimeDir), "logos_qml_runtime.js", "// glue");
        write(QDir(m_runtimeDir), "logos_qml_runtime.wasm", QByteArray("\0asm", 4));
        // Two real files OUTSIDE both roots, one reachable by `..` from each:
        // a traversal test against a path that does not exist would pass on the
        // 404 alone and prove nothing.
        write(root, "modules/secret.txt", "not yours");
        write(root, "secret.txt", "not yours either");
    }

    // ── the load path ──────────────────────────────────────────────────────

    void theEntryIsServedOnTheSchemeAndNeverAsAFile()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        const QUrl url = bridge->entryUrl();
        QCOMPARE(url.scheme(), QString("logos"));
        QCOMPARE(url.host(), QString("module"));
        QCOMPARE(url.path(), QString("/index.html"));
        // The criterion is literal: a file:// page is a unique opaque origin
        // that may fetch neither its own QML nor the runtime.
        QVERIFY(!url.toString().contains("file:"));
        // And nothing here opens a socket, so there is no port to bind.
        QCOMPARE(url.port(), -1);
    }

    void itServesThePackageAndTheRuntimeFromOneOrigin()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());

        auto page = request(*bridge, "GET", "/index.html");
        QVERIFY(page->answered);
        QCOMPARE(page->reply.status, 200);
        QCOMPARE(page->reply.mimeType, QByteArray("text/html"));
        QCOMPARE(page->reply.filePath, QDir(m_moduleDir).filePath("index.html"));

        auto qml = request(*bridge, "GET", "/view/Main.qml");
        QCOMPARE(qml->reply.status, 200);
        // FETCHED AS TEXT and compiled by the runtime, not downloaded.
        QCOMPARE(qml->reply.mimeType, QByteArray("text/plain"));

        auto runtime = request(*bridge, "GET", "/logos-runtime/logos_qml_runtime.wasm");
        QCOMPARE(runtime->reply.status, 200);
        // What lets instantiateStreaming take the 26 MB image without buffering.
        QCOMPARE(runtime->reply.mimeType, QByteArray("application/wasm"));
        QCOMPARE(runtime->reply.filePath, QDir(m_runtimeDir).filePath("logos_qml_runtime.wasm"));
    }

    void aRequestOutsideBothRootsIsRefused()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        for (const char* path : { "/../secret.txt", "/view/../../secret.txt",
                                  "/logos-runtime/../secret.txt", "/nothing-here.js" }) {
            auto call = request(*bridge, "GET", QString::fromLatin1(path));
            QVERIFY2(call->answered, path);
            QCOMPARE(call->reply.status, 404);
            QVERIFY2(call->reply.filePath.isEmpty(), path);
        }
    }

    // ── the channel ────────────────────────────────────────────────────────

    void theShimCarriesTheRuntimeBaseAndTheLaunchToken()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        const QString shim = bridge->channelShim();
        QVERIFY(shim.contains("logos://module/logos-runtime/"));
        QVERIFY(shim.contains(bridge->launchToken()));
        QVERIFY(shim.contains("logosChannelReady"));
    }

    void everyLaunchGetsItsOwnToken()
    {
        std::unique_ptr<MobileWebBridge> a(make());
        std::unique_ptr<MobileWebBridge> b(make());
        QVERIFY(!a->launchToken().isEmpty());
        QVERIFY(a->launchToken() != b->launchToken());
    }

    void aPostedFrameReachesTheReceiver()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        std::vector<std::string> got;
        bridge->setReceiver([&got](const std::string& text) { got.push_back(text); });

        auto call = request(*bridge, "POST", control(*bridge, "send"), R"({"type":"Call"})");
        QVERIFY(call->answered);
        QCOMPARE(call->reply.status, 200);
        QCOMPARE(got.size(), size_t(1));
        QCOMPARE(QString::fromStdString(got[0]), QString(R"({"type":"Call"})"));
    }

    void aChunkedFrameIsReassembledAndDeliveredOnce()
    {
        // THE PATH A PHONE ACTUALLY TAKES. A frame that needed three requests
        // must arrive once, whole, and in one piece -- a receiver that saw the
        // chunks would be handed three malformed transport frames.
        std::unique_ptr<MobileWebBridge> bridge(make());
        std::vector<std::string> got;
        bridge->setReceiver([&got](const std::string& text) { got.push_back(text); });

        const QString frame = QStringLiteral("{\"type\":\"Call\",\"payload\":\"%1\"}")
                                  .arg(QString(9000, QLatin1Char('x')));
        postChunked(*bridge, frame, 0);
        QCOMPARE(got.size(), size_t(1));
        QCOMPARE(QString::fromStdString(got[0]), frame);
    }

    void aChunkThatArrivesTwiceDoesNotCompleteTheFrameEarly()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        std::vector<std::string> got;
        bridge->setReceiver([&got](const std::string& text) { got.push_back(text); });

        const auto chunk = [&](int index, int count, const char* data) {
            QUrl url;
            url.setScheme("logos");
            url.setHost("module");
            url.setPath(control(*bridge, "send"));
            QUrlQuery query;
            query.addQueryItem("s", "7");
            query.addQueryItem("i", QString::number(index));
            query.addQueryItem("n", QString::number(count));
            query.addQueryItem("d", QLatin1String(data));
            url.setQuery(query);
            bridge->handleRequest("GET", url, {}, [](const BridgeReply&) {});
        };

        chunk(0, 2, "he");
        chunk(0, 2, "he");      // the webview retried
        QVERIFY(got.empty());
        chunk(1, 2, "llo");
        QCOMPARE(got.size(), size_t(1));
        QCOMPARE(QString::fromStdString(got[0]), QString("hello"));
    }

    void twoFramesInFlightDoNotInterleave()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        std::vector<std::string> got;
        bridge->setReceiver([&got](const std::string& text) { got.push_back(text); });
        postChunked(*bridge, QStringLiteral("first frame, which is longer"), 1, 4);
        postChunked(*bridge, QStringLiteral("second"), 2, 4);
        QCOMPARE(got.size(), size_t(2));
        QCOMPARE(QString::fromStdString(got[0]), QString("first frame, which is longer"));
        QCOMPARE(QString::fromStdString(got[1]), QString("second"));
    }

    void aSendMissingItsChunkFieldsIsRefused()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        bool delivered = false;
        bridge->setReceiver([&delivered](const std::string&) { delivered = true; });
        auto call = request(*bridge, "GET", control(*bridge, "send"));
        QVERIFY(call->answered);
        QCOMPARE(call->reply.status, 400);
        QVERIFY(!delivered);
    }

    void aFrameWithTheWrongTokenIsRefusedAndNotDelivered()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        bool delivered = false;
        bridge->setReceiver([&delivered](const std::string&) { delivered = true; });

        auto call = request(*bridge, "POST", "/__logos/not-the-token/send", "{}");
        QVERIFY(call->answered);
        QCOMPARE(call->reply.status, 403);
        QVERIFY(!delivered);
    }

    void theControlPrefixIsNeverServedAsADocument()
    {
        // A package that ships a `__logos/` directory must not be able to
        // shadow the channel — nor to have its files served out of it.
        QDir().mkpath(QDir(m_moduleDir).filePath("__logos"));
        write(QDir(m_moduleDir), "__logos/smuggled.js", "// nope");
        std::unique_ptr<MobileWebBridge> bridge(make());
        auto call = request(*bridge, "GET", "/__logos/smuggled.js");
        QVERIFY(call->answered);
        QVERIFY(call->reply.status != 200);
        QVERIFY(call->reply.filePath.isEmpty());
    }

    void aPollTakesWhatSendQueued()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        QVERIFY(bridge->send("one"));
        QVERIFY(bridge->send("two"));

        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(call->answered);
        QCOMPARE(call->reply.mimeType, QByteArray("application/json"));
        QCOMPARE(framesIn(call->reply), (QStringList{"one", "two"}));

        // ...and only once.
        auto again = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(!again->answered);
    }

    void aPollThatFindsNothingIsParkedUntilAFrameArrives()
    {
        // THE BUFFER IS THE POINT. The container starts talking the moment it
        // has published the module — the first thing it sends is the wildcard
        // event SUBSCRIBE — and a page still booting its 26 MB runtime has not
        // polled yet. A bridge that answered "nothing" and forgot would lose it.
        std::unique_ptr<MobileWebBridge> bridge(make());
        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(!call->answered);

        QVERIFY(bridge->send("late"));
        QVERIFY(call->answered);
        QCOMPARE(framesIn(call->reply), QStringList{"late"});
    }

    void aParkedPollIsAnsweredEmptyWhenItExpires()
    {
        // A long poll the page leaves open forever is a request the webview
        // never completes; the page re-arms on an empty batch.
        std::unique_ptr<MobileWebBridge> bridge(make());
        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(!call->answered);
        bridge->expireWaits();
        QVERIFY(call->answered);
        QCOMPARE(call->reply.status, 200);
        QVERIFY(framesIn(call->reply).isEmpty());
    }

    void framesQueuedBeforeThePageEverPollsAreNotLost()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        for (int i = 0; i < 5; ++i) bridge->send(QStringLiteral("f%1").arg(i).toStdString());
        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QCOMPARE(framesIn(call->reply), (QStringList{"f0", "f1", "f2", "f3", "f4"}));
    }

    // ── death ──────────────────────────────────────────────────────────────

    void thePagesConsoleReachesTheHost()
    {
        // A page that died on line one is indistinguishable from one that has
        // not come up yet -- unless it says so, and this is the only route it
        // has on a phone.
        std::unique_ptr<MobileWebBridge> bridge(make());
        QStringList lines;
        bridge->setOnPageLog([&lines](const QString& level, const QString& message) {
            lines.append(level + ": " + message);
        });

        QUrl url;
        url.setScheme("logos");
        url.setHost("module");
        url.setPath(control(*bridge, "log"));
        QUrlQuery query;
        query.addQueryItem("l", "error");
        query.addQueryItem("m", QUrl::toPercentEncoding("could not load logos-view-loader.js"));
        url.setQuery(query);
        auto call = std::make_shared<Call>();
        bridge->handleRequest("GET", url, {}, [call](const BridgeReply& r) {
            call->answered = true;
            call->reply = r;
        });
        QVERIFY(call->answered);
        QCOMPARE(lines, QStringList{"error: could not load logos-view-loader.js"});
    }

    void thePageClosingItsChannelIsReported()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        int closed = 0;
        bridge->setOnPageClosed([&closed] { ++closed; });

        auto call = request(*bridge, "POST", control(*bridge, "close"));
        QVERIFY(call->answered);
        QCOMPARE(closed, 1);
        QVERIFY(!bridge->isOpen());
        QVERIFY(!bridge->send("gone"));
    }

    void closingTheHostEndAnswersAParkedPoll()
    {
        // Otherwise the page's fetch never settles and its loader hangs on a
        // promise while the container has already torn the module down.
        std::unique_ptr<MobileWebBridge> bridge(make());
        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(!call->answered);
        bridge->close();
        QVERIFY(call->answered);
        QVERIFY(call->reply.body.contains("\"closed\":true"));
    }

    void aClosedBridgeServesNothingFurther()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        bridge->close();
        auto call = request(*bridge, "GET", control(*bridge, "poll"));
        QVERIFY(call->answered);
        QVERIFY(call->reply.body.contains("\"closed\":true"));
        QVERIFY(!bridge->send("x"));
    }

    void detachingTheReceiverStopsDelivery()
    {
        std::unique_ptr<MobileWebBridge> bridge(make());
        int delivered = 0;
        bridge->setReceiver([&delivered](const std::string&) { ++delivered; });
        request(*bridge, "POST", control(*bridge, "send"), "{}");
        QCOMPARE(delivered, 1);
        bridge->setReceiver(nullptr);
        request(*bridge, "POST", control(*bridge, "send"), "{}");
        QCOMPARE(delivered, 1);
    }
};

QTEST_MAIN(MobileWebBridgeTest)
#include "mobile_web_bridge_test.moc"
