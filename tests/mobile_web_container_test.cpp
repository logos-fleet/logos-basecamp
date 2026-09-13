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
    // What the host asked the page to run. A `web` variant draws into a canvas,
    // so driving a real key or a real finger at it is the one conversation that
    // does not go down the channel.
    QStringList scripts;
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
    QString m_bareModuleDir;
    QString m_coreModuleDir;
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
            platform.evaluateJavaScript = [page](const QString& script) {
                page->scripts.append(script);
            };
            return platform;
        };
    }

    // `counter_ui` is a module whose package ships a headless entry document and
    // `notes_ui` is one whose package does not -- which is the fork every
    // eviction takes, and the only difference between the two directories.
    QString dirFor(const QString& module) const
    {
        if (module == QLatin1String("notes_ui")) return m_bareModuleDir;
        if (module == QLatin1String("keystore_module")) return m_coreModuleDir;
        return m_moduleDir;
    }

    LogosCore::WebModuleViewRequest requestFor(const QString& module) const
    {
        const QString dir = dirFor(module);
        LogosCore::WebModuleViewRequest request;
        request.moduleName = module.toStdString();
        request.moduleDir = dir.toStdString();
        request.entryPath = QDir(dir).filePath("index.html").toStdString();
        return request;
    }

    // The launch token the bridge minted, read back out of the script it asked
    // the platform to inject. A test driving a control path has to speak as the
    // page does, and the page only knows the token because the shim carries it.
    static QString tokenOf(const QString& shim)
    {
        const int at = shim.indexOf("/__logos/");
        if (at < 0) return {};
        const QString rest = shim.mid(at + 9);
        return rest.left(rest.indexOf(QLatin1Char('\'')));
    }

    static void writeFile(const QString& path, const QByteArray& content)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
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
        m_bareModuleDir = QDir(m_root.path()).filePath("notes_ui");
        m_coreModuleDir = QDir(m_root.path()).filePath("keystore_module");
        m_runtimeDir = QDir(m_root.path()).filePath("logos-runtime");
        QDir().mkpath(m_moduleDir);
        QDir().mkpath(m_bareModuleDir);
        QDir().mkpath(m_coreModuleDir);
        QDir().mkpath(m_runtimeDir);

        // A package as logos-module-builder emits one: two entry documents, and
        // a manifest that names the headless one.
        writeFile(QDir(m_moduleDir).filePath("index.html"), "<!doctype html><body></body>");
        writeFile(QDir(m_moduleDir).filePath("host.html"), "<!doctype html><body>host</body>");
        writeFile(QDir(m_moduleDir).filePath("manifest.json"),
                  R"({"name":"counter_ui","main":"index.html",
                      "logos_web_runtime":"qml",
                      "logos_web_view":{"qml":"view/Counter.qml","headless":"host.html"}})");

        // ...and a package from before they existed.
        writeFile(QDir(m_bareModuleDir).filePath("index.html"), "<!doctype html><body></body>");
        writeFile(QDir(m_bareModuleDir).filePath("manifest.json"),
                  R"({"name":"notes_ui","main":"index.html","logos_web_runtime":"qml"})");

        // ...and a `core` module's `web` variant, which is the first kind whose
        // page is NOT a user interface: a Worker, a wasm image and an empty
        // body (logos-module-builder's buildWebModule.nix).
        writeFile(QDir(m_coreModuleDir).filePath("index.html"), "<!doctype html><body></body>");
        writeFile(QDir(m_coreModuleDir).filePath("manifest.json"),
                  R"({"name":"keystore_module","main":"index.html","type":"core",
                      "logos_web_runtime":"wasm"})");
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
        // The page's OWN origin, which is the module's -- see the next case.
        QUrl url = m_pages[0]->loaded.resolved(QUrl("/index.html"));
        m_pages[0]->serve("GET", url, {}, [&reply](const BridgeReply& r) { reply = r; });
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.mimeType, QByteArray("text/html"));
    }

    // ONE ORIGIN PER MODULE. A `web` variant's durable store is IndexedDB and a
    // browser keys IndexedDB by origin, so two modules served on one origin are
    // two modules in one store -- able to read and clobber each other's state
    // with nothing in either able to tell. The mobile containers have no
    // per-page profile to separate them with (iOS shares one WKWebsiteDataStore,
    // Android one WebView data directory); the origin is the lever they have.
    void twoWebModulesAreServedOnTwoOrigins()
    {
        auto counter = load("counter_ui");
        auto notes = load("notes_ui");
        QVERIFY(counter != nullptr);
        QVERIFY(notes != nullptr);
        QCOMPARE(m_pages.size(), size_t(2));

        const QString counterHost = m_pages[0]->loaded.host();
        const QString notesHost = m_pages[1]->loaded.host();
        QCOMPARE(counterHost, QString("counter-ui.module"));
        QCOMPARE(notesHost, QString("notes-ui.module"));
        QVERIFY(counterHost != notesHost);

        // ...and each page's bridge answers on its own origin and NOT on the
        // other's, so a document that got the host wrong is refused rather than
        // served out of a stranger's package.
        BridgeReply mine;
        m_pages[0]->serve("GET", m_pages[0]->loaded.resolved(QUrl("/index.html")), {},
                          [&mine](const BridgeReply& r) { mine = r; });
        QCOMPARE(mine.status, 200);

        BridgeReply theirs;
        m_pages[0]->serve("GET", m_pages[1]->loaded.resolved(QUrl("/index.html")), {},
                          [&theirs](const BridgeReply& r) { theirs = r; });
        QCOMPARE(theirs.status, 404);
    }

    // A PAGE IS NOT A UI, and the first module for which the two differ is a
    // `core` one: a `web` variant of it is a Worker, a wasm image and an empty
    // body. It still gets a page -- that is where the module RUNS -- and a
    // Shell that read the page as evidence of a user interface gave it a
    // sidebar tile that opens onto nothing.
    void aCoreModulesPageIsNotAUi()
    {
        auto keystore = load("keystore_module");
        QVERIFY(keystore != nullptr);
        auto* backend = MobileWebContainerBackend::instance();
        QVERIFY(backend->hasView("keystore_module"));
        QVERIFY(!backend->pageServesUi("keystore_module"));

        // ...while a ui_qml package's page is, and so is one whose manifest
        // predates the question.
        auto counter = load("counter_ui");
        auto notes = load("notes_ui");
        QVERIFY(backend->pageServesUi("counter_ui"));
        QVERIFY(backend->pageServesUi("notes_ui"));
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

    // THE EVICTION SLICE 28 ASKS FOR: the module gives its UI page up and keeps
    // answering. Its package ships a headless entry document, so the backend
    // swaps the page onto it rather than asking the host to unload the module.
    void showingASecondModuleBackgroundsTheFirstWithoutUnloadingIt()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);
        QSignalSpy unloadNeeded(backend, &MobileWebContainerBackend::uiEvictionRequired);

        auto first = load("counter_ui");
        auto second = load("notes_ui");

        QCOMPARE(backend->show("counter_ui"), QStringList{});
        QCOMPARE(backgrounded.count(), 0);

        QCOMPARE(backend->show("notes_ui"), QStringList{"counter_ui"});
        QCOMPARE(backgrounded.count(), 1);
        QCOMPARE(backgrounded.at(0).at(0).toString(), QString("counter_ui"));
        // NOT the other signal: there is nowhere for the host to send this and
        // nothing for it to unload.
        QCOMPARE(unloadNeeded.count(), 0);

        // The UI page is gone and a headless one is in its place.
        QVERIFY(m_pages[0]->destroyed);
        QCOMPARE(m_pages.size(), size_t(3));
        QCOMPARE(m_pages[2]->moduleName, QString("counter_ui"));
        QCOMPARE(m_pages[2]->loaded.path(), QString("/host.html"));
        QVERIFY(m_pages[2]->alive);

        // ...AND THE MODULE IS STILL THERE, which is the whole point: same view
        // object, same channel, still published, still answering. A container
        // that had unloaded it would have taken all three away.
        QVERIFY(backend->hasView("counter_ui"));
        QVERIFY(!backend->hasUiPage("counter_ui"));
        QVERIFY(first->isAlive());
        QVERIFY(first->channel()->isOpen());
    }

    // A frame the core sends while the swap is in flight is not lost: the
    // bridge buffers it exactly as it buffers the first Subscribe while a page
    // is booting, and the incoming page's first poll takes it. This is what
    // "keeps answering calls" means when the call arrives at the worst moment.
    void aFrameSentAcrossTheSwapIsDeliveredToTheHeadlessPage()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");

        QVERIFY(first->channel()->send("{\"type\":1}"));

        BridgeReply reply;
        QUrl poll = m_pages[2]->loaded;
        poll.setPath("/__logos/" + tokenOf(m_pages[2]->shim) + "/poll");
        m_pages[2]->serve("GET", poll, {}, [&reply](const BridgeReply& r) { reply = r; });
        QCOMPARE(reply.status, 200);
        QVERIFY2(reply.body.contains("type"), reply.body.constData());
    }

    // ...and back again. A budget that only ever took pages away would leave a
    // user staring at the module they just chose.
    void showingABackgroundedModuleBringsItsUiBack()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");
        QVERIFY(!backend->hasUiPage("counter_ui"));

        QSignalSpy unloadNeeded(backend, &MobileWebContainerBackend::uiEvictionRequired);
        backend->show("counter_ui");
        QVERIFY(backend->hasUiPage("counter_ui"));
        QCOMPARE(m_pages.back()->moduleName, QString("counter_ui"));
        QCOMPARE(m_pages.back()->loaded.path(), QString("/index.html"));
        // On screen, not merely alive: a page comes up at the BACK of the
        // hierarchy on both phones, and a restored one has to be brought
        // forward like any other.
        QVERIFY(m_pages.back()->frontmost);
        // And `notes_ui` has gone the other way. It ships no headless document,
        // so it is handed to the host -- which is why its UI page is STILL
        // there at this point: nothing here destroys it.
        QCOMPARE(unloadNeeded.count(), 1);
        QCOMPARE(unloadNeeded.at(0).at(0).toString(), QString("notes_ui"));
        QVERIFY(backend->hasUiPage("notes_ui"));
    }

    // THE OLDER ANSWER, for a package built before headless documents existed.
    // Announced and not performed: the page belongs to liblogos' container and
    // is still there until the host unloads the module through the core.
    void aModuleWithNoHeadlessDocumentIsHandedToTheHostToUnload()
    {
        auto* backend = MobileWebContainerBackend::instance();
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);
        QSignalSpy unloadNeeded(backend, &MobileWebContainerBackend::uiEvictionRequired);

        auto first = load("notes_ui");
        auto second = load("counter_ui");
        backend->show("notes_ui");
        QCOMPARE(backend->show("counter_ui"), QStringList{"notes_ui"});

        QCOMPARE(unloadNeeded.count(), 1);
        QCOMPARE(unloadNeeded.at(0).at(0).toString(), QString("notes_ui"));
        QCOMPARE(backgrounded.count(), 0);
        QVERIFY(backend->hasView("notes_ui"));
        QVERIFY(!m_pages[0]->destroyed);
    }

    // The one conversation with a module that does not go down the channel. A
    // `web` variant draws into a canvas, so a host showing that a real key or a
    // real finger reaches the view has to put the event in the page.
    void aHostCanDriveInputAtAPage()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto view = load("counter_ui");
        QVERIFY(backend->runJavaScriptIn("counter_ui", "logosPress('a')"));
        QCOMPARE(m_pages[0]->scripts.size(), 1);
        QCOMPARE(m_pages[0]->scripts.at(0), QString("logosPress('a')"));
        QVERIFY(!backend->runJavaScriptIn("no_such_module", "logosPress('a')"));
    }

    void anEvictedModuleIsNotAskedTwice()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto first = load("counter_ui");
        auto second = load("notes_ui");

        backend->show("counter_ui");
        backend->show("notes_ui");
        // The module went away for a reason of its own -- unloaded, uninstalled
        // -- which destroys its view through the ordinary container path.
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

    // THE WORKSPACE IS BACK ON THE SHELL'S OWN CHROME. Closing an app is not
    // showing another one, and it is not an eviction either -- the module stays
    // loaded and keeps answering, its page simply stops covering the Shell.
    //
    // It cannot be spelled `show("")`: the budget would enter a visible module
    // under a name no module has, and the next real show() would evict against
    // a phantom. Nothing about the books changes here; only the surface.
    void hidingEveryPagePutsTheHostsOwnSurfaceBack()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(2));
        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        QVERIFY(m_pages[0]->frontmost);

        const int liveBefore = backend->budget().live().size();
        backend->hideAll();

        QVERIFY(!m_pages[0]->frontmost);
        QVERIFY(!m_pages[1]->frontmost);
        // Still loaded, still in the books: a module whose UI is not on screen
        // is not a module that was given up.
        QCOMPARE(backend->loadedModules().size(), 2);
        QCOMPARE(backend->budget().live().size(), liveBefore);
        QVERIFY(backend->hasUiPage("counter_ui"));
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
        QCOMPARE(backend->budget().budgetBytes(), LiveRuntimeBudget::kDeviceRuntimeBytes);
    }
};

QTEST_MAIN(MobileWebContainerTest)
#include "mobile_web_container_test.moc"
