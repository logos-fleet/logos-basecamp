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
#include <QRect>
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
    // WHERE THE SHELL PUT IT. A page is mounted at the WINDOW's size, so a page
    // that is merely brought forward covers the Shell's own navigation and the
    // user is inside an app with no way out (#110). The Shell answers by saying
    // what rect its workspace left for a web app's page, and this is that rect
    // arriving at the platform half.
    QRect geometry;
    int geometryCalls = 0;
    // What the host asked the page to run. A `web` variant draws into a canvas,
    // so driving a real key or a real finger at it is the one conversation that
    // does not go down the channel.
    QStringList scripts;
    std::function<void(const QByteArray&, const QUrl&, const QByteArray&,
                       basecamp::web::MobileWebBridge::Respond)> serve;
    std::function<void()> die;
    int address = 0;
};

// WHAT THE CONTAINER SAID WHILE THIS WAS ALIVE.
//
// The budget is LOGGED rather than returned -- slice 28 asks the host to say
// what it is holding, and a number the host never prints cannot be read off a
// device run. So the console IS the interface for the accounting, and
// logos-workspace#151 is a report of it saying two things at once: the Shell
// announcing that nothing is mounted while the container's last word was still
// "web_counter is visible; 1 live runtime(s)". Lines are forwarded to the
// handler that was in place, so a failing test still prints everything QtTest
// would have printed.
QStringList* g_captured = nullptr;
QtMessageHandler g_previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (g_captured) g_captured->append(message);
    if (g_previousHandler) g_previousHandler(type, context, message);
}

class ConsoleLines {
public:
    ConsoleLines()
    {
        g_captured = &m_lines;
        g_previousHandler = qInstallMessageHandler(captureMessage);
    }
    ~ConsoleLines()
    {
        qInstallMessageHandler(g_previousHandler);
        g_captured = nullptr;
        g_previousHandler = nullptr;
    }

    QStringList matching(const QString& needle) const
    {
        QStringList found;
        for (const QString& line : m_lines) {
            if (line.contains(needle)) found << line;
        }
        return found;
    }
    QString joined() const { return m_lines.join(QLatin1Char('\n')); }

private:
    QStringList m_lines;
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
            platform.setGeometry = [page](const QRect& rect) {
                page->geometry = rect;
                ++page->geometryCalls;
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

    // The page this module is on RIGHT NOW. A swap tears the platform page down
    // and builds a second one from the same ingredients, so the first entry for
    // a module stops being the one on screen the moment it has been backgrounded
    // and brought back.
    std::shared_ptr<FakeWebView> currentPageFor(const QString& module) const
    {
        for (auto it = m_pages.rbegin(); it != m_pages.rend(); ++it) {
            if ((*it)->moduleName == module && !(*it)->destroyed) return *it;
        }
        return nullptr;
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

        // And the user opens it again, which is what pressing its tile does
        // (#123): a fresh page, and the books name what is there rather than
        // what used to be. The module that gives its UI up here is `notes_ui`;
        // naming the destroyed page a second time is a use after free on every
        // platform this runs on.
        auto reopened = load("counter_ui");
        QSignalSpy evict(backend, &MobileWebContainerBackend::uiEvictionRequired);
        QCOMPARE(backend->show("counter_ui"), QStringList{"notes_ui"});
        QCOMPARE(evict.count(), 1);
        QCOMPARE(evict.at(0).at(0).toString(), QString("notes_ui"));
    }

    // ── #151: THE BOOKS MAY ONLY NAME A MODULE THAT HAS A PAGE ─────────────
    //
    // A `web` app unloaded from the Modules tab left the Shell holding a tab
    // onto a page that no longer existed, and raising that tab said so out
    // loud: `web_counter is visible; 1 live runtime(s), 290 MB of 290 MB` from
    // this container, beside `app web_counter is not mounted` from the Shell.
    // The tab is #149's half and is fixed; this is the container's, and it is
    // the half that costs something -- the phone's single live runtime was
    // spent on a module with no page, so the next app to open had to evict a
    // dead one.
    //
    // show() is an ANNOUNCEMENT from the Shell ("the user is looking at this"),
    // and an announcement about a module this container has no page for is
    // stale by definition: the page is gone and what is left to do is say so,
    // not to write it into the books.
    void aModuleWithNoPageIsNotShown()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto view = load("counter_ui");
        backend->show("counter_ui");
        QCOMPARE(backend->budget().visible(), QStringLiteral("counter_ui"));

        // Unloaded from the Modules tab: the core tears the view down, which is
        // the one path a page goes away by.
        view.reset();
        QVERIFY(!backend->hasView("counter_ui"));

        ConsoleLines console;
        QCOMPARE(backend->show("counter_ui"), QStringList{});
        QVERIFY2(backend->budget().live().isEmpty(),
                 qPrintable(backend->budget().live().join(QLatin1Char(','))));
        QCOMPARE(backend->budget().visible(), QString());
        QCOMPARE(backend->frontmostModule(), QString());
        QCOMPARE(backend->budget().projectedBytes(), Q_INT64_C(0));
        // AND IT SAYS SO. The console is the whole of a phone's diagnostic
        // surface, and a container that stayed silent here is what left the
        // Shell's "not mounted" as the only account of the state.
        QVERIFY2(!console.matching(QStringLiteral("counter_ui has no page")).isEmpty(),
                 qPrintable(console.joined()));
        QVERIFY2(console.matching(QStringLiteral("counter_ui is visible")).isEmpty(),
                 qPrintable(console.joined()));
    }

    // AND IT DOES NOT COST THE MODULE THAT IS ACTUALLY UP. With the budget at
    // one, entering a page-less module in the books names the live one for
    // eviction -- so a stale tab press took down the app the user was looking
    // at, to make room for a module that no longer exists.
    void aStaleShowDoesNotEvictTheModuleThatIsUp()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto gone = load("counter_ui");
        backend->show("counter_ui");
        gone.reset();

        auto up = load("notes_ui");
        backend->show("notes_ui");
        QCOMPARE(backend->budget().live(), QStringList{"notes_ui"});

        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);
        QSignalSpy unloadNeeded(backend, &MobileWebContainerBackend::uiEvictionRequired);
        QCOMPARE(backend->show("counter_ui"), QStringList{});

        QCOMPARE(unloadNeeded.count(), 0);
        QCOMPARE(backgrounded.count(), 0);
        QCOMPARE(backend->budget().live(), QStringList{"notes_ui"});
        QCOMPARE(backend->budget().visible(), QStringLiteral("notes_ui"));
        QCOMPARE(backend->frontmostModule(), QStringLiteral("notes_ui"));
        QVERIFY(currentPageFor("notes_ui")->frontmost);
    }

    // A PAGE GOING AWAY IS THE OTHER HALF OF THE SAME SENTENCE. show() prints
    // what the container is holding; nothing printed when it stopped holding
    // it, so the last word about `web_counter` on the console of the run #151
    // was reported from was that it was visible and alive -- which was true
    // when it was printed and was never taken back.
    void aPageGoingAwayIsAnnouncedWithWhatIsLeft()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto view = load("counter_ui");
        backend->show("counter_ui");

        ConsoleLines console;
        view.reset();

        const QStringList said = console.matching(QStringLiteral("counter_ui"));
        QVERIFY2(!said.filter(QStringLiteral("page is gone")).isEmpty(),
                 qPrintable(console.joined()));
        // The count and the budget, as show() states them: the two numbers a
        // device run is read against.
        QVERIFY2(!said.filter(QStringLiteral("0 live runtime(s)")).isEmpty(),
                 qPrintable(console.joined()));
        // ...and what the app weighs now it is not holding the page, which is
        // the pair slice 28 asks for -- the memory with a module's UI live, and
        // what it returns to.
        QVERIFY2(!said.filter(QStringLiteral("app memory")).isEmpty(),
                 qPrintable(console.joined()));
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
        QCOMPARE(backend->frontmostModule(), QStringLiteral("notes_ui"));
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
        // THE SURFACE, not the books. The budget still names the last module the
        // user looked at -- nothing was given up -- and nothing is in front.
        QCOMPARE(backend->frontmostModule(), QString());
        QCOMPARE(backend->budget().visible(), QStringLiteral("counter_ui"));
    }

    // ── #110: A PAGE GOES WHERE THE SHELL LEFT ROOM FOR IT ─────────────────
    //
    // A page is mounted at the window's size and brought forward when the user
    // opens the app, which on a phone covers the Shell's navigation entirely:
    // there is then no sidebar, no tab bar and no way back out of the app. The
    // Shell's answer is to dock a PLACEHOLDER for a web app exactly as it docks
    // a `ui_qml` one and to say where the workspace put it -- so the page lands
    // inside the Shell's content area and the chrome around it stays live.
    //
    // Window coordinates, Qt logical pixels: the Shell measures a widget and the
    // platform half converts. An empty rect is "the whole window", which is what
    // a page gets before anyone has said otherwise.
    void theShellSaysWhereAPageGoes()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(2));
        auto view = load("counter_ui");
        QCOMPARE(backend->contentRect(), QRect());
        QCOMPARE(m_pages[0]->geometryCalls, 0);

        backend->setContentRect(QRect(96, 48, 720, 960));

        QCOMPARE(backend->contentRect(), QRect(96, 48, 720, 960));
        QCOMPARE(m_pages[0]->geometry, QRect(96, 48, 720, 960));
    }

    // A page opened AFTER the Shell measured its workspace goes there too. The
    // order is not the host's to pick: a module is loaded whenever the core
    // discovers or the user installs one, and the content area was measured once
    // at the first mount.
    void aPageOpenedLaterGoesWhereTheShellAlreadySaid()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(2));
        backend->setContentRect(QRect(0, 64, 800, 1000));

        auto view = load("counter_ui");

        QCOMPARE(m_pages[0]->geometry, QRect(0, 64, 800, 1000));
    }

    // AND IT KEEPS ITS PLACE ACROSS A BACKGROUND TRIP. An eviction destroys the
    // platform page and builds a second one from the same ingredients
    // (MobileWebModuleView::swapPageTo), so a rect that lived only in the
    // platform half would be forgotten exactly once -- and the module the user
    // came back to would come back covering the Shell again.
    void aPageKeepsItsPlaceAcrossABackgroundTrip()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(1));
        backend->setContentRect(QRect(10, 20, 300, 400));
        auto first = load("counter_ui");
        auto second = load("notes_ui");

        backend->show("counter_ui");
        backend->show("notes_ui");     // counter_ui is backgrounded: a new page
        backend->show("counter_ui");   // ...and its UI comes back: another one

        auto page = currentPageFor("counter_ui");
        QVERIFY(page);
        QCOMPARE(page->geometry, QRect(10, 20, 300, 400));
    }

    // The Shell's chrome came back, so the page is the window's again. Symmetry
    // matters here: the reset is what a host does when no app is docked, and a
    // container that only ever narrowed a page would leave the last app's
    // letterbox on screen for the next module that opened one.
    void clearingTheContentRectGivesThePageTheWindowBack()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(2));
        auto view = load("counter_ui");
        backend->setContentRect(QRect(10, 20, 300, 400));

        QCOMPARE(m_pages[0]->geometryCalls, 1);

        backend->setContentRect(QRect());

        QCOMPARE(backend->contentRect(), QRect());
        // TOLD, not merely forgotten: the page is where the last app left it
        // until the platform is asked to move it back.
        QCOMPARE(m_pages[0]->geometryCalls, 2);
        QCOMPARE(m_pages[0]->geometry, QRect());
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

    // ── #153: THE MEASUREMENT IS READ, AND THE OS IS LISTENED TO ───────────
    //
    // The container measured what the app weighed and printed it; nothing
    // decided anything with it, and there was no memory-pressure handling in
    // mobile/ at all. Both are pinned here.

    // A ceiling of one byte is over the ceiling whatever this machine is doing,
    // which is how a container test says "the app has grown" without being able
    // to make it grow.
    void aPageIsGivenUpWhenTheAppIsOverItsCeiling()
    {
        auto* backend = MobileWebContainerBackend::instance();
        // A COUNT OF THREE, so an eviction here cannot be the count: two pages
        // are well inside it and only the weighing can take one away.
        backend->install(m_runtimeDir, fakePlatform(),
                         LiveRuntimeBudget(3, LiveRuntimeBudget::kDeviceRuntimeBytes, 1));
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);

        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");

        QCOMPARE(backgrounded.count(), 1);
        QCOMPARE(backgrounded.at(0).at(0).toString(), QString("counter_ui"));
        QCOMPARE(backend->budget().live(), QStringList{"notes_ui"});
        QVERIFY(backend->hasUiPage("notes_ui"));
    }

    // THE CROSSING IS ANNOUNCED EVEN WHEN NOTHING IS SHED (logos-workspace#244).
    //
    // With ONE page live an observation over the ceiling takes nothing away --
    // one page is the floor -- it only tightens the allowance, and the eviction
    // that causes happens at the NEXT show(), several lines later. Logged only
    // on an eviction, the moment the ceiling was crossed left no trace at all,
    // which is the same failure as the ceiling that could never trip: a device
    // run cannot tell a policy that acted from one that was never reached.
    // Measured on a Xiaomi 25028RN03Y with `--web-ceiling 1440` on 2026-09-17:
    // the figure crossed at the first page and the log said nothing until the
    // second one was opened.
    void crossingTheCeilingIsAnnouncedEvenWithNothingToShed()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(),
                         LiveRuntimeBudget(3, LiveRuntimeBudget::kDeviceRuntimeBytes, 1));
        auto only = load("counter_ui");
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);

        ConsoleLines console;
        backend->show("counter_ui");

        QCOMPARE(backgrounded.count(), 0);
        QVERIFY(backend->hasUiPage("counter_ui"));
        QCOMPARE(backend->budget().liveAllowance(), 1);
        QVERIFY2(!console.matching(QStringLiteral("the allowance goes")).isEmpty(),
                 qPrintable(console.joined()));
    }

    // ...and the same container with no ceiling keeps both, which is what says
    // the line above was the ceiling and not something else.
    void insideTheCeilingBothPagesStay()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(3));
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);

        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");

        QCOMPARE(backgrounded.count(), 0);
        QCOMPARE(backend->budget().live().size(), 2);
    }

    // THE ONE SIGNAL THAT IS NOT THIS APP'S OPINION. iOS posts a memory warning
    // and then kills the app; Android calls onTrimMemory. Either arrives here,
    // and everything but the module the user is looking at gives its page up.
    void aMemoryWarningKeepsOnlyTheVisiblePage()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(3));
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);

        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");
        QCOMPARE(backend->budget().live().size(), 2);

        ConsoleLines console;
        backend->memoryWarning();

        QCOMPARE(backgrounded.count(), 1);
        QCOMPARE(backgrounded.at(0).at(0).toString(), QString("counter_ui"));
        QCOMPARE(backend->budget().live(), QStringList{"notes_ui"});
        // The module is still loaded and still answering -- a warning is not an
        // unload, it is the same eviction the budget makes for its own reasons.
        QVERIFY(backend->hasView("counter_ui"));
        QVERIFY(first->isAlive());
        // AND IT IS ANNOUNCED. A shed nobody can see in the log is a shed
        // nobody can attribute a cold start to afterwards.
        QVERIFY2(!console.matching(QStringLiteral("memory warning")).isEmpty(),
                 qPrintable(console.joined()));
        QVERIFY2(!console.matching(QStringLiteral("live runtime(s)")).isEmpty(),
                 qPrintable(console.joined()));
    }

    // A warning with nothing to give up is not an error and not a teardown: the
    // visible page stays, because a container with no UI at all is not a
    // container that saved anything.
    void aMemoryWarningWithOnePageLeavesItAlone()
    {
        auto* backend = MobileWebContainerBackend::instance();
        auto view = load("counter_ui");
        backend->show("counter_ui");
        QSignalSpy backgrounded(backend, &MobileWebContainerBackend::uiEvicted);

        backend->memoryWarning();

        QCOMPARE(backgrounded.count(), 0);
        QVERIFY(backend->hasUiPage("counter_ui"));
    }

    // ...and the budget stays tightened afterwards, so the next app the user
    // opens does not put the app straight back to the size the OS complained
    // about.
    void afterAWarningTheNextShowStillKeepsOne()
    {
        auto* backend = MobileWebContainerBackend::instance();
        backend->install(m_runtimeDir, fakePlatform(), LiveRuntimeBudget(3));
        auto first = load("counter_ui");
        auto second = load("notes_ui");
        backend->show("counter_ui");
        backend->show("notes_ui");
        backend->memoryWarning();
        QCOMPARE(backend->budget().liveAllowance(), 1);

        // notes_ui ships no headless document, so its eviction is the other
        // signal -- the host unloads it. Which signal fires is the package's
        // business; that ONE of them fires is the budget's.
        QSignalSpy unloadNeeded(backend, &MobileWebContainerBackend::uiEvictionRequired);
        backend->show("counter_ui");
        QCOMPARE(unloadNeeded.count(), 1);
        QCOMPARE(unloadNeeded.at(0).at(0).toString(), QString("notes_ui"));
    }

    // WHAT THE DEVICE AFFORDS, stated once at install. A device run is read
    // against this line: it is the only place the count, the ceiling and the
    // device's own memory appear together.
    void theContainerStatesWhatTheDeviceAffords()
    {
        ConsoleLines console;
        MobileWebContainerBackend::instance()->install(
            m_runtimeDir, fakePlatform(),
            LiveRuntimeBudget(2, LiveRuntimeBudget::kDeviceRuntimeBytes,
                              4LL * 1024 * 1024 * 1024));
        const QStringList said = console.matching(QStringLiteral("live-runtime budget"));
        QVERIFY2(!said.isEmpty(), qPrintable(console.joined()));
        QVERIFY2(said.filter(QStringLiteral("2 runtimes")).size() == 1,
                 qPrintable(said.join(QLatin1Char('\n'))));
        // The ceiling belongs in the same sentence: a run that shows the app at
        // 900 MB is only readable beside the number that would have shed a page.
        QVERIFY2(said.filter(QStringLiteral("4096 MB")).size() == 1,
                 qPrintable(said.join(QLatin1Char('\n'))));
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
