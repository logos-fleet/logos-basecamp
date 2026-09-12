// srcdeps: webview/MobileWebContainerBackend.cpp webview/MobileWebModuleView.cpp webview/MobileWebBridge.cpp webview/LiveRuntimeBudget.cpp webview/AppMemory.cpp web/LogosWebPaths.cpp
//
// THE PHONE'S WEB CONTAINER BACKEND, driven with a FAKE WEBVIEW.
//
// What a platform contributes to a Web container on a phone is four things: a
// webview that routes `logos://module/...` at the bridge, a script injected
// before the page's own, a URL to load, and a report when the page dies. All
// four are arguments here (PlatformPageFactory), so the container's own
// behaviour -- publish the view, hold it, expire its polls, hand its surface to
// the shell, and enforce the live-runtime budget -- is testable with no UIKit
// and no JNI in the room. What is left over on each platform is the twenty
// lines that call handleRequest().
//
// THE EVICTION RULE IS THE INTERESTING ONE. The backend must ANNOUNCE that a
// module is over budget and not act: the page belongs to liblogos' container,
// which destroys it when the module is unloaded, and a backend that destroyed a
// view behind the container's back would leave a published module with a dead
// channel. So the assertion is that uiEvictionRequired fires and the view is
// STILL THERE until the host unloads it.
//
// Run: nix build .#unit-tests -L

#include "webview/AppMemory.h"
#include "webview/MobileWebContainerBackend.h"

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

// liblogos' two process-global seams, stubbed.
//
// This test links neither liblogos nor logos-protocol: it takes their HEADERS
// (the view seam and the channel interface, which is all the backend codes
// against) and supplies the two symbols they declare. Linking the real library
// would pull a core, a loader and a transport into a check that drives none of
// them.
namespace LogosCore {
static WebModuleViewFactory g_factory;
void setWebModuleViewFactory(WebModuleViewFactory factory) { g_factory = std::move(factory); }
WebModuleViewFactory webModuleViewFactory() { return g_factory; }
} // namespace LogosCore

using basecamp::web::BridgeReply;
using basecamp::web::LiveRuntimeBudget;
using basecamp::web::MobileWebContainerBackend;
using basecamp::web::PlatformPage;
using basecamp::web::PlatformPageRequest;

namespace {

// What a platform's webview would have done, recorded instead.
struct FakeWebView {
    QString moduleName;
    QUrl loaded;
    QString shim;
    bool destroyed = false;
    bool alive = true;
    // What the shell asked of the surface. A page is mounted BEHIND the host's
    // own view and brought forward when the user is looking at that module, so
    // "which module is visible" is a fact about the platform view and not only
    // about the budget's bookkeeping.
    bool frontmost = false;
    int frontmostCalls = 0;
    std::function<void(const QByteArray&, const QUrl&, const QByteArray&,
                       basecamp::web::MobileWebBridge::Respond)> serve;
    std::function<void()> die;
    int address = 0;
};

} // namespace

class MobileWebContainerTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QString m_moduleDir;
    QString m_runtimeDir;
    std::vector<std::shared_ptr<FakeWebView>> m_pages;

    basecamp::web::PlatformPageFactory fakePlatform()
    {
        return [this](const PlatformPageRequest& request) {
            auto page = std::make_shared<FakeWebView>();
            page->moduleName = request.moduleName;
            page->loaded = request.entryUrl;
            page->shim = request.channelShim;
            page->serve = request.serve;
            page->die = request.onDied;
            m_pages.push_back(page);

            PlatformPage platform;
            platform.destroy = [page] { page->destroyed = true; page->alive = false; };
            platform.nativeHandle = [page] { return static_cast<void*>(&page->address); };
            platform.isAlive = [page] { return page->alive; };
            platform.setFrontmost = [page](bool front) {
                page->frontmost = front;
                ++page->frontmostCalls;
            };
            return platform;
        };
    }

    LogosCore::WebModuleViewRequest requestFor(const QString& module) const
    {
        LogosCore::WebModuleViewRequest request;
        request.moduleName = module.toStdString();
        request.moduleDir = m_moduleDir.toStdString();
        request.entryPath = QDir(m_moduleDir).filePath("index.html").toStdString();
        return request;
    }

    std::unique_ptr<LogosCore::WebModuleView> load(const QString& module)
    {
        return LogosCore::webModuleViewFactory()(requestFor(module));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_moduleDir = QDir(m_root.path()).filePath("counter_ui");
        m_runtimeDir = QDir(m_root.path()).filePath("logos-runtime");
        QDir().mkpath(m_moduleDir);
        QDir().mkpath(m_runtimeDir);
        QFile entry(QDir(m_moduleDir).filePath("index.html"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("<!doctype html><body></body>");
        entry.close();
    }

    void init()
    {
        m_pages.clear();
        MobileWebContainerBackend::instance()->install(m_runtimeDir, fakePlatform(),
                                                       LiveRuntimeBudget(1));
    }

    void loadingAWebModuleOpensAPageAndAnnouncesIt()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QSignalSpy opened(backend, &MobileWebContainerBackend::viewOpened);

        auto view = load("counter_ui");
        QVERIFY(view != nullptr);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), QString("counter_ui"));
        QVERIFY(backend->hasView("counter_ui"));
        QVERIFY(backend->nativeHandleFor("counter_ui") != nullptr);

        QCOMPARE(m_pages.size(), size_t(1));
        // Never a file: URL — a file:// page is a unique opaque origin and may
        // fetch neither its own QML nor the bundled runtime (ADR 0004).
        QCOMPARE(m_pages[0]->loaded.scheme(), QString("logos"));
        QCOMPARE(m_pages[0]->loaded.path(), QString("/index.html"));
        QVERIFY(m_pages[0]->shim.contains("logosChannelReady"));
    }

    void thePagesRequestsReachTheBridge()
    {
        auto view = load("counter_ui");
        QVERIFY(view != nullptr);

        BridgeReply reply;
        QUrl url;
        url.setScheme("logos");
        url.setHost("module");
        url.setPath("/index.html");
        m_pages[0]->serve("GET", url, {}, [&reply](const BridgeReply& r) { reply = r; });
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.mimeType, QByteArray("text/html"));
    }

    void aDestroyedViewLeavesTheRegistry()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QSignalSpy closed(backend, &MobileWebContainerBackend::viewClosed);

        auto view = load("counter_ui");
        QVERIFY(backend->hasView("counter_ui"));
        view.reset();

        QCOMPARE(closed.count(), 1);
        QVERIFY(!backend->hasView("counter_ui"));
        QVERIFY(m_pages[0]->destroyed);
    }

    void aDeadPageIsReportedOnce()
    {
        auto view = load("counter_ui");
        int died = 0;
        view->setOnDied([&died] { ++died; });

        m_pages[0]->alive = false;
        m_pages[0]->die();
        m_pages[0]->die();
        QCOMPARE(died, 1);
        QVERIFY(!view->isAlive());
        // A dead page's channel is closed, so the container's relay stops
        // rather than writing into a webview that is gone.
        QVERIFY(!view->channel()->isOpen());
    }

    // ── the budget ─────────────────────────────────────────────────────────

    void showingASecondModuleAsksTheFirstForItsPage()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QSignalSpy evict(backend, &MobileWebContainerBackend::uiEvictionRequired);

        auto first = load("counter_ui");
        auto second = load("notes_ui");

        QCOMPARE(backend->show("counter_ui"), QStringList{});
        QCOMPARE(evict.count(), 0);

        QCOMPARE(backend->show("notes_ui"), QStringList{"counter_ui"});
        QCOMPARE(evict.count(), 1);
        QCOMPARE(evict.at(0).at(0).toString(), QString("counter_ui"));

        // ANNOUNCED, NOT PERFORMED: the page belongs to liblogos' container and
        // is still there until the host unloads the module through the core.
        QVERIFY(backend->hasView("counter_ui"));
        QVERIFY(!m_pages[0]->destroyed);
    }

    void anEvictedModuleIsNotAskedTwice()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto first = load("counter_ui");
        auto second = load("notes_ui");

        backend->show("counter_ui");
        backend->show("notes_ui");
        // The host answered the eviction: the module was unloaded, which
        // destroys its view through the ordinary container path.
        first.reset();

        QSignalSpy evict(backend, &MobileWebContainerBackend::uiEvictionRequired);
        backend->show("counter_ui");
        QCOMPARE(evict.count(), 1);
        QCOMPARE(evict.at(0).at(0).toString(), QString("notes_ui"));
    }

    // The shell says what the user is looking at; the surface has to follow,
    // because a page is mounted at the BACK of the hierarchy on both phones and
    // would otherwise never be seen however healthy its channel is.
    void showingAModuleBringsItsPageToTheFrontAndSendsTheOthersBack()
    {
        auto* backend = MobileWebContainerBackend::instance();
        // A budget of two, so both pages stay and the only thing under test is
        // which one is in front.
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(2));
        auto first = load("counter_ui");
        auto second = load("notes_ui");

        backend->show("counter_ui");
        QVERIFY(m_pages[0]->frontmost);
        QVERIFY(!m_pages[1]->frontmost);

        backend->show("notes_ui");
        QVERIFY(!m_pages[0]->frontmost);
        QVERIFY(m_pages[1]->frontmost);
    }

    // The host's own cost, which is what slice 28's "memory returns to within a
    // stated budget" is measured in. A platform that will not say answers -1;
    // every platform this runs on says something.
    void theAppCanWeighItself()
    {
        const qint64 bytes = basecamp::web::appResidentBytes();
        QVERIFY2(bytes > 0, qPrintable(QStringLiteral("appResidentBytes() = %1").arg(bytes)));
        // Sanity rather than a threshold: a Qt test process is more than a
        // megabyte and less than a hundred gigabytes, and a parse that read the
        // wrong column would fail one of the two.
        QVERIFY(bytes > 1024 * 1024);
        QVERIFY(bytes < Q_INT64_C(100) * 1024 * 1024 * 1024);
    }

    void theBudgetIsStated()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QCOMPARE(backend->budget().maxLiveRuntimes(), 1);
        QCOMPARE(backend->budget().budgetBytes(), LiveRuntimeBudget::kSpikeRuntimeBytes);
    }
};

QTEST_MAIN(MobileWebContainerTest)
#include "mobile_web_container_test.moc"
