// The liblogos smoke host: liblogos_core running on a phone with the app's
// Bundled set in it. It starts the core against a persistence path inside the
// app sandbox, registers and loads every member of the set, prints the verdicts
// and the protocol version to the screen and to the platform console, and stays
// alive until Quit is pressed -- at which point the core is cleaned up before
// the process exits, so a hung shutdown is visible as a hang and not as a kill.
//
// WHICH modules the set holds is `ws build --bundle`'s answer, resolved from
// the catalog at build time and recorded in the manifest compiled into this
// host. Nothing in this file names one.
#include <QtGlobal>

// The Bundled VIEW module is iOS-only, so everything that reaches it is behind
// this one name. On Android Qt is a set of SHARED objects, so a ui_qml module
// there is a different artifact with a different gate and logos-module-builder
// publishes no `view` output for it -- so no Android Bundled set can carry a
// view module, and android/CMakeLists.txt accordingly neither compiles
// ViewModuleRunner nor finds Qt Quick. (The Android SHELL does compile it: it
// is the same BundledSetShellHost on both phones and that is what mounts one.)
#if defined(Q_OS_IOS)
#  define LOGOS_SMOKE_WITH_VIEW_MODULE 1
#endif

#include "BundledSetCoreRuntime.h"
#include "webview/MobileWebContainerBackend.h"
#include "webview/WebPageProbe.h"
#if defined(Q_OS_IOS)
#include "webview/IosWebPage.h"
#elif defined(Q_OS_ANDROID)
#include "webview/AndroidWebPage.h"
#endif
#include "BundledSetRunner.h"
#include "NetworkSmokeRunner.h"
#include "PlatformConsole.h"
#include "SmokeRunner.h"
#include "WebModuleRunner.h"
#if defined(LOGOS_SMOKE_WITH_VIEW_MODULE)
#include "ViewModuleRunner.h"
#endif

#include <QApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMetaObject>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QVector>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWidget>
#if defined(LOGOS_SMOKE_WITH_VIEW_MODULE)
#include <QQuickWidget>
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {

QPlainTextEdit* g_log = nullptr;

void console(const char* tag, const QString& line)
{
    basecamp::mobile::consoleLine(tag, line);
}

// THE ON-SCREEN LOG, FROM WHATEVER THREAD. Qt Widgets may only be touched from
// the GUI thread, and lines reach here from several others: Android serves a
// webview's requests on a Chromium thread, and liblogos traces from its module
// workers. Appending directly corrupts the QTextDocument -- measured on a
// Samsung SM-G990B, 2026-09-12, twice: a SIGSEGV inside
// QTextDocumentPrivate::insert while a page logged, and later a SIGSEGV reading
// a block's text back during an input-method query, which is the same damage
// showing up one frame later.
void appendToLog(const QString& line)
{
    if (!g_log) return;
    if (QThread::currentThread() == g_log->thread()) {
        g_log->appendPlainText(line);
        return;
    }
    QMetaObject::invokeMethod(g_log, [line] {
        if (g_log) g_log->appendPlainText(line);
    }, Qt::QueuedConnection);
}

void say(const QString& line)
{
    console("smoke", line);
    appendToLog(line);
}

// Clean shutdown on request, where the request is a signal: the Quit button
// is for a human, and an automated smoke run (simctl terminate, adb am
// force-stop -> SIGTERM) needs the same path. A handler may only touch
// async-signal-safe things, so it writes one byte to a pipe and Qt's event
// loop does the real work.
int g_signalPipe[2] = { -1, -1 };

void onTerminate(int)
{
    const char b = 1;
    ssize_t ignored = ::write(g_signalPipe[1], &b, 1);
    (void)ignored;
}

void qtMessages(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    // The core traces every LogosAPI call; all of it goes to the platform
    // console, and only warnings and above compete for the small screen.
    console("qt", msg);
    if (type >= QtWarningMsg) appendToLog("qt: " + msg);
}

} // namespace

int main(int argc, char* argv[])
{
    // FIRST STATEMENT, so "since main()" is the truth. Everything below it --
    // QApplication, the window, the core -- is time the user spends looking at
    // a launch image, and a clock started after the window was built would
    // report the cold start as shorter than it is.
    QElapsedTimer sinceMain;
    sinceMain.start();

    QApplication app(argc, argv);
    app.setOrganizationName("Logos");
    app.setApplicationName("LiblogosSmoke");
    qInstallMessageHandler(qtMessages);

    QMainWindow window;
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
#if defined(LOGOS_SMOKE_WITH_VIEW_MODULE)
    // The VIEW module's surface: a Qt Quick scene inside this Widgets host,
    // so the log below it stays on screen. The module's QML is loaded into
    // THIS engine -- the host's own -- which is the whole shape of a Bundled
    // view module on a phone.
    auto* viewSurface = new QQuickWidget;
    viewSurface->setResizeMode(QQuickWidget::SizeRootObjectToView);
    viewSurface->setMinimumHeight(240);
    layout->addWidget(viewSurface, 2);
#endif
    // The Modules list sits above the log, filled in once the set is known.
    auto* modulesPanel = new QWidget;
    auto* modulesLayout = new QVBoxLayout(modulesPanel);
    modulesLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(modulesPanel, 0);

    auto* logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    auto* quit = new QPushButton(QStringLiteral("Quit"));
    layout->addWidget(logView, 1);
    layout->addWidget(quit, 0);
    g_log = logView;
    window.setCentralWidget(central);
    window.showFullScreen();
    // The probe's OWN surface -- a log view and a Quit button -- and it is
    // named differently from the Shell's marker on purpose: the two are not
    // the same screen, and a report should not be able to pretend they are.
    // Both phones have a Shell of their own now
    // (../basecamp-shell/README.md), so this number is the probe's, never a
    // stand-in for one.
    say(QStringLiteral("COLD START: host UI shown at %1 ms").arg(sinceMain.elapsed()));

    SmokeRunner runner;
    QObject::connect(&runner, &SmokeRunner::log, &say);

    // The runtime seam. Everything below asks THIS about modules -- what is
    // known, what is loaded, load, unload -- and never the C API, exactly as
    // Basecamp does on the desktop. What is different on a phone is where the
    // installed set comes from: a manifest compiled in at build time rather
    // than a directory scanned at runtime.
    // THE WEB CONTAINER'S BACKEND, BEFORE THE CORE. liblogos registers the Web
    // container in every process and leaves the webview unset, so a `web`
    // module loaded before this ran would report the missing bridge rather than
    // opening a page. Installing it here -- not at the first load -- is also
    // what pins the Qt main thread as the one a webview is built on.
    //
    // The budget is the phone's: ONE live QML runtime, which the spike measured
    // at 185-240 MB. A tablet could afford more and this is where that decision
    // would be made.
    QString webModulesDir;
    {
        using basecamp::web::MobileWebContainerBackend;
        auto* web = MobileWebContainerBackend::instance();
#if defined(Q_OS_IOS)
        webModulesDir = basecamp::web::iosWebModulesDir();
        web->install(basecamp::web::iosQmlRuntimeDir(),
                     basecamp::web::iosPlatformPageFactory());
#elif defined(Q_OS_ANDROID)
        // BEFORE THE CONTAINER, because the runtime directory the container is
        // installed with has to exist by then: an APK's assets are not files,
        // so the app's `web` half is copied out once into the data directory.
        basecamp::web::unpackAndroidWebAssets(QStringLiteral(LOGOS_WEB_ASSETS_STAMP));
        webModulesDir = basecamp::web::androidWebModulesDir();
        // ANDROID DIFFERS TWICE. The shim travels INSIDE the entry document,
        // because there is no user-script API and evaluateJavascript runs after
        // the page's own first script; and the page is served over https,
        // because Chromium's Fetch there refuses a non-standard scheme even
        // when the embedder registered one -- measured on a Samsung, see
        // LogosWebPaths.h.
        web->install(basecamp::web::androidQmlRuntimeDir(),
                     basecamp::web::androidPlatformPageFactory(),
                     basecamp::web::LiveRuntimeBudget(), /*shimInDocument=*/true,
                     basecamp::web::WebOrigin::android());
#endif
        QObject::connect(web, &MobileWebContainerBackend::uiEvictionRequired,
                         &app, [](const QString& name) {
                             say(QStringLiteral("web container: %1 is over the "
                                                "live-runtime budget and must give "
                                                "up its UI page").arg(name));
                         });
    }

#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    // THE ONE THING A DESKTOP CANNOT ANSWER: does this platform's webview
    // deliver a request to its interceptor when the page is entered under Qt's
    // separate-main-stack entry? That is where the mobile round-trip spike
    // found WKScriptMessageHandler trapping, and the reason the channel is a
    // URL scheme at all. A fixture page, a frame each way, on the console --
    // before any module is loaded, so a failure here is not mistaken for one.
    {
#if defined(Q_OS_IOS)
        basecamp::web::WebPageProbe probe(basecamp::web::iosPlatformPageFactory(),
                                          /*shimInDocument=*/false);
#else
        basecamp::web::WebPageProbe probe(basecamp::web::androidPlatformPageFactory(),
                                          /*shimInDocument=*/true,
                                          basecamp::web::WebOrigin::android());
#endif
        QObject::connect(&probe, &basecamp::web::WebPageProbe::log, &say);
        say(probe.run() ? QStringLiteral("web container: PASS")
                        : QStringLiteral("web container: FAIL"));
    }
#endif

    ICoreRuntime::Config coreConfig = runner.prepare(argc, argv);
    // THE APP'S SHIPPED `web` MODULES, as a second modules directory. Unlike
    // the Bundled set -- which is native code and therefore fixed in the app
    // image (ADR 0003) -- a `web` variant is wasm and JavaScript, so it is
    // DISCOVERED the way the desktop discovers an installed package: a
    // directory per module with its manifest at the top. Read-only on iOS
    // (inside the app bundle) and unpacked on Android, and the core does not
    // care which.
    if (!webModulesDir.isEmpty())
        coreConfig.modulesDirs.push_back(webModulesDir.toStdString());
    BundledSetCoreRuntime core(coreConfig);
    QObject::connect(&core, &BundledSetCoreRuntime::log, &say);
    core.start();
    runner.report();
    say(QStringLiteral("core up, %1 ms since main()").arg(sinceMain.elapsed()));

    // The app's Bundled SET, brought up in the Native container. It is run
    // after the core is up and before the event loop, so the verdict is on
    // screen and in the platform console by the time the first frame is drawn
    // -- an automated run reads it off the console and never has to tap
    // anything.
    //
    // WHICH modules these are is `ws build --bundle`'s answer, resolved from
    // the catalog and recorded in the manifest compiled into this host. Nothing
    // here names one.
    BundledSetRunner bundled(&core);
    QObject::connect(&bundled, &BundledSetRunner::log, &say);
    const bool bundledOk = bundled.run() && bundled.callCounter();
    say(bundledOk ? QStringLiteral("bundled module: PASS")
                  : QStringLiteral("bundled module: FAIL"));

    // ...and what the networking modules DO once they are up, if --bundle put
    // any of them in the set: a libp2p node created and started, dialled at a
    // desktop peer and exchanging a gossipsub message, and a chat conversation
    // over delivery_module. A set without them says so and passes -- loading is
    // the container's job and is already reported above; this is the modules'
    // own.
    NetworkSmokeRunner network(&core);
    QObject::connect(&network, &NetworkSmokeRunner::log, &say);
    QObject::connect(&network, &NetworkSmokeRunner::chatUsable, &app, [&sinceMain]() {
        say(QStringLiteral("COLD START: Chat usable at %1 ms").arg(sinceMain.elapsed()));
    });
    if (network.hasWork()) {
        const bool networkOk = network.run();
        say(networkOk ? QStringLiteral("networking modules: PASS")
                      : QStringLiteral("networking modules: FAIL"));
    } else {
        say(QStringLiteral("networking modules: none in this Bundled set"));
    }

    // ...and the app's Downloaded WEB modules: a `ui_qml` module's `web`
    // variant loaded through the same core into a real webview, driven, and
    // weighed against the live-runtime budget. This is slice 28's subject and
    // the one thing no desktop check can answer -- whether a Qt-wasm QML
    // runtime boots in THIS platform's webview.
    //
    // After the Bundled set, because the page's own calls go out as the
    // module's identity and their first hop is capability_module, which the
    // core brings up at start; and after the networking modules, so a slow
    // 25 MB runtime does not delay a verdict that has nothing to do with it.
    WebModuleRunner webModules(&core, webModulesDir);
    QObject::connect(&webModules, &WebModuleRunner::log, &say);
    const bool webOk = webModules.run();
    say(webOk ? QStringLiteral("web modules: PASS")
              : QStringLiteral("web modules: FAIL"));

#if defined(LOGOS_SMOKE_WITH_VIEW_MODULE)
    // ...and the app's Bundled VIEW module, if --bundle put one in the set,
    // brought up the same way and for the same reason: on screen and on the
    // console before the first frame, so an automated run reads the verdict
    // off the console.
    ViewModuleRunner view;
    QObject::connect(&view, &ViewModuleRunner::log, &say);
    const bool hasView = bundled.hasViewModule();
    const bool viewOk = hasView && view.run(viewSurface);
    if (!hasView) {
        viewSurface->hide();
        say(QStringLiteral("view module: none in this Bundled set"));
    } else {
        say(viewOk ? QStringLiteral("view module: PASS")
                   : QStringLiteral("view module: FAIL"));
    }

    // Then press the view's own button once, after the first frame -- the
    // scene has no geometry before it, so there is no button to press yet.
    // An automated run reads the round trip off the console; a human sees the
    // number change on screen and can press it again.
    if (viewOk)
        QTimer::singleShot(1500, &view, &ViewModuleRunner::driveViewOnce);
#endif

    // ── the Modules list ──────────────────────────────────────────────────
    // What a Shell's Modules tab shows, in the smallest form a Widgets host
    // can: one row per member of the Bundled set, its state read back from the
    // runtime, and a button that loads or unloads it through the Native
    // container. The set is fixed -- a Store shell cannot gain a native module
    // at runtime (ADR 0003) -- but what is RUNNING is the user's to change.
    struct ModuleRow { QString name; QLabel* state; QPushButton* toggle; };
    QVector<ModuleRow> rows;
    auto refreshRows = [&rows, &core]() {
        const QStringList loaded = core.loadedModules();
        for (const ModuleRow& row : rows) {
            const bool on = loaded.contains(row.name);
            row.state->setText(on ? QStringLiteral("loaded") : QStringLiteral("not loaded"));
            row.toggle->setText(on ? QStringLiteral("Unload") : QStringLiteral("Load"));
        }
    };
    for (const QVariant& value : core.bundledSet()) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value("name").toString();
        const bool isView = entry.value("type").toString() == QLatin1String("ui_qml");

        auto* line = new QWidget;
        auto* rowLayout = new QHBoxLayout(line);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(QStringLiteral("%1 %2")
                                            .arg(name, entry.value("version").toString())), 1);
        auto* state = new QLabel;
        rowLayout->addWidget(state, 0);
        auto* toggle = new QPushButton;
        rowLayout->addWidget(toggle, 0);
        modulesLayout->addWidget(line);

        if (isView) {
            // Host-loaded, not core-registered: the core has no opinion about
            // it, so neither has this row (ADR 0006, and see ViewModuleRunner).
            state->setText(QStringLiteral("view, host-loaded"));
            toggle->setEnabled(false);
            toggle->setText(QStringLiteral("Load"));
            continue;
        }
        rows.append({ name, state, toggle });
        QObject::connect(toggle, &QPushButton::clicked, &core, [&core, &refreshRows, name]() {
            if (core.loadedModules().contains(name))
                core.unloadModule(name, /*withDependents=*/true);
            else
                core.loadModule(name);
            refreshRows();
        });
    }
    refreshRows();

    // Press the Modules list's own buttons once, after the first frame -- the
    // button is the Shell's surface, so driving it is the only way to claim
    // that load/unload FROM THE SHELL works rather than that the C API does.
    // An automated run reads the two transitions off the console; a human sees
    // the row change and can press it again.
    if (!rows.isEmpty()) {
        QTimer::singleShot(2000, &core, [&core, &rows]() {
            const ModuleRow& row = rows.first();
            const bool before = core.loadedModules().contains(row.name);
            row.toggle->click();
            const bool afterFirst = core.loadedModules().contains(row.name);
            row.toggle->click();
            const bool afterSecond = core.loadedModules().contains(row.name);
            say(QStringLiteral("drive modules: %1 %2 -> %3 -> %4")
                    .arg(row.name)
                    .arg(before ? "loaded" : "not loaded")
                    .arg(afterFirst ? "loaded" : "not loaded")
                    .arg(afterSecond ? "loaded" : "not loaded"));
            say(before && !afterFirst && afterSecond
                    ? QStringLiteral("MODULES TAB ROUND TRIP OK")
                    : QStringLiteral("WRONG: unload then load did not round-trip"));
        });
    }

    auto shutdown = [&]() {
        runner.stop();
        say(QStringLiteral("exiting"));
#if defined(Q_OS_IOS)
        // Measured on an iPad Air (iPadOS, signed device build): app.exec()
        // returns 0 and main() returns, and the process THEN dies with SIGSEGV
        // -- after the core is down and after "event loop returned 0" prints,
        // so the fault is in the teardown of a fully static iOS image (global
        // destructors, and Qt's iOS run-loop integration unwinding the
        // separate stack it runs main() on), not in liblogos. Nothing useful
        // happens after this point on iOS -- the OS reclaims everything -- so
        // exit here, where the shutdown that MATTERS (logos_core_cleanup) has
        // just been logged, rather than crash a few frames later and report it
        // as a failed exit.
        std::fflush(nullptr);
        std::_Exit(0);
#else
        app.quit();
#endif
    };
    QObject::connect(quit, &QPushButton::clicked, shutdown);

    if (::pipe(g_signalPipe) == 0) {
        auto* notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, [&, notifier]() {
            notifier->setEnabled(false);
            char b;
            ssize_t ignored = ::read(g_signalPipe[0], &b, 1);
            (void)ignored;
            say(QStringLiteral("SIGTERM/SIGINT: shutting down"));
            shutdown();
        });
        std::signal(SIGTERM, onTerminate);
        std::signal(SIGINT, onTerminate);
    } else {
        say(QStringLiteral("pipe() failed; only the Quit button can stop this"));
    }

    const int rc = app.exec();
    say(QStringLiteral("event loop returned %1").arg(rc));
    return rc;
}
