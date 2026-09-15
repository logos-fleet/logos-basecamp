// Basecamp's Shell on a phone, over the app's Bundled set.
//
// The same three pieces the desktop app has, and no fourth:
//
//   the Shell     main_ui, the real one -- src/, the identical sources the
//                 desktop plugin is built from. Qt for iOS is static, so it is
//                 LINKED IN and reached through QPluginLoader::staticInstances()
//                 rather than dlopened; IShellView and its ABI check are
//                 untouched (ADR 0001).
//   the seam      IShellHost, also untouched. BundledSetShellHost answers it.
//   the runtime   ICoreRuntime, answered by BundledSetCoreRuntime off the
//                 Bundled-set manifest the build compiled in. There is no
//                 modules directory on a phone to scan (ADR 0003).
//
// WHICH modules the Shell lists is `ws build --bundle`'s answer, resolved from
// the catalog at build time. Nothing in this file names one.
#include "BundledSetCoreRuntime.h"
#include "BundledSetShellHost.h"
#include "appmanager/CatalogSource.h"
#include "appmanager/ConsentScript.h"
#include "appmanager/ModuleCallScript.h"
#include "appmanager/ModuleDirectories.h"
#include "IShellHost.h"
#include "IShellView.h"
#include "NetworkSmokeRunner.h"
#include "PlatformConsole.h"
#include "QuickWidgetKeyboardFocus.h"
#include "ShellAppDriver.h"
#include "ShellCallDriver.h"
#include "ShellCatalogDriver.h"
#include "ShellConsentDriver.h"
#include "ShellKeyboardDriver.h"
#include "ShellModulesDriver.h"
#include "ShellWebAppDriver.h"
#include "ShellSections.h"
#include "SmokeRunner.h"
#include "web/LogosWebPaths.h"
#include "webview/MobileWebContainerBackend.h"
#if defined(Q_OS_IOS)
#include "webview/IosWebPage.h"
#elif defined(Q_OS_ANDROID)
#include "webview/AndroidWebPage.h"
#endif

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPluginLoader>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

// main_ui built with MAIN_UI_STATIC. moc emitted qt_static_plugin_MainShellView
// inside that archive; this is what pulls it in and registers it, and without
// it staticInstances() is empty and the app has no UI at all.
Q_IMPORT_PLUGIN(MainShellView)

namespace {

void console(const QString& line)
{
    basecamp::mobile::consoleLine("shell", line);
}

void qtMessages(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    // QML warnings are the interesting output of a shell that renders a
    // scene it has never rendered on this platform before, so they go to the
    // platform console like everything else.
    basecamp::mobile::consoleLine("qt", msg);
}

// Clean shutdown on a signal -- simctl terminate sends SIGTERM, and the core
// must come down before the process does. A handler may only touch
// async-signal-safe things, so it writes one byte and the event loop does the
// real work.
int g_signalPipe[2] = { -1, -1 };

void onTerminate(int)
{
    const char b = 1;
    ssize_t ignored = ::write(g_signalPipe[1], &b, 1);
    (void)ignored;
}

IShellView* resolveShell()
{
    for (QObject* instance : QPluginLoader::staticInstances()) {
        if (auto* shell = qobject_cast<IShellView*>(instance))
            return shell;
    }
    return nullptr;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("Logos");
    app.setApplicationName("BasecampShell");
    qInstallMessageHandler(qtMessages);

    QElapsedTimer sinceMain;
    sinceMain.start();

    // THE ON-SCREEN KEYBOARD, BEFORE ANY SCENE EXISTS. The Shell's QML and a
    // mounted app's both live in QQuickWidgets, whose focus is the offscreen
    // window's and not the one the platform input context is told about -- and
    // on iOS nothing ever hands the widget the window's focus, so a tapped
    // field took a caret and no keyboard (logos-workspace#152). This watches
    // every QQuickWidget in the process, including the one an app is mounted
    // into long after startup, which is why it is installed here rather than
    // handed a list of surfaces.
    auto* keyboardFocus = new QuickWidgetKeyboardFocus(&app);
    keyboardFocus->watchEverything();

    SmokeRunner runner;
    QObject::connect(&runner, &SmokeRunner::log, &console);

    // The runtime seam. Everything above it asks THIS about modules, exactly
    // as Basecamp does on the desktop; what differs on a phone is where the
    // installed set comes from.
    //
    // THE DIRECTORIES, BEFORE THE CORE STARTS, because a module directory can
    // only be added before logos_core_start(). Everything a Downloaded module
    // needs to be FOUND is decided here: package_manager installs into
    // `installModulesDir` and the core scans `coreModulesDirs`, and
    // ModuleDirectories is what keeps those the same place.
    const QString appDataRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    // THE WEB CONTAINER, BEFORE THE CORE AND BEFORE THE DIRECTORIES. liblogos
    // registers the Web container in every process and leaves the webview
    // unset, so a `web` module loaded before this ran would report the missing
    // bridge rather than opening a page -- and installing it HERE rather than
    // at the first load is also what pins the Qt main thread as the one a
    // webview is built on.
    //
    // This is the half a Store shell could not do until now. A Downloaded
    // module IS a `web` variant (a phone may not download native code, ADR
    // 0003), so without a container the App Manager could install one and
    // there was nowhere for it to run.
    //
    // The budget is the phone's: ONE live QML runtime, measured at 185-240 MB.
    QString shippedWebModulesDir;
    {
        using basecamp::web::MobileWebContainerBackend;
        auto* web = MobileWebContainerBackend::instance();
#if defined(Q_OS_IOS)
        shippedWebModulesDir = basecamp::web::iosWebModulesDir();
        web->install(basecamp::web::iosQmlRuntimeDir(),
                     basecamp::web::iosPlatformPageFactory());
#elif defined(Q_OS_ANDROID)
        // BEFORE THE CONTAINER: an APK's assets are not files, so the app's
        // `web` half is copied out once into the data directory and the
        // runtime directory the container is installed with has to exist by
        // then.
        basecamp::web::unpackAndroidWebAssets(QStringLiteral(LOGOS_WEB_ASSETS_STAMP));
        shippedWebModulesDir = basecamp::web::androidWebModulesDir();
        // Android differs twice: the shim travels INSIDE the entry document
        // (no user-script API) and the page is served over https (Chromium's
        // Fetch refuses a non-standard scheme there). See LogosWebPaths.h.
        web->install(basecamp::web::androidQmlRuntimeDir(),
                     basecamp::web::androidPlatformPageFactory(),
                     basecamp::web::LiveRuntimeBudget(), /*shimInDocument=*/true,
                     basecamp::web::WebOrigin::android());
#endif
    }

    const basecamp::appmanager::ModuleDirectories moduleDirs =
        basecamp::appmanager::ModuleDirectories::under(appDataRoot, shippedWebModulesDir);
    QDir().mkpath(moduleDirs.installModulesDir);
    QDir().mkpath(moduleDirs.installUiPluginsDir);

    ICoreRuntime::Config coreConfig = runner.prepare(argc, argv);
    for (const QString& dir : moduleDirs.coreModulesDirs) {
        const std::string entry = dir.toStdString();
        if (std::find(coreConfig.modulesDirs.begin(), coreConfig.modulesDirs.end(), entry)
            == coreConfig.modulesDirs.end())
            coreConfig.modulesDirs.push_back(entry);
    }
    BundledSetCoreRuntime core(coreConfig);
    QObject::connect(&core, &BundledSetCoreRuntime::log, &console);
    core.start();
    runner.report();
    console(QStringLiteral("core up, %1 ms since main()").arg(sinceMain.elapsed()));

    IShellView* shell = resolveShell();
    if (!shell)
        qFatal("No static plugin implements IShellView; was main_ui linked with Q_IMPORT_PLUGIN?");
    // The same check the desktop host makes. It is not redundant under a
    // static link: main_ui and this host are separate stages, so they can
    // still be built against different revisions of IShellHost.h.
    if (shell->hostAbiVersion() != IShellHost_abi)
        qFatal("shell was built against IShellHost ABI %d, host is %d",
               shell->hostAbiVersion(), IShellHost_abi);
    console(QStringLiteral("shell: IShellHost ABI %1 (host %2)")
                .arg(shell->hostAbiVersion()).arg(IShellHost_abi));

    BundledSetShellHost host(&core);
    QObject::connect(host.backend(), &ShellModulesBackend::log, &console);
    // The container is installed; this is the Shell listening to it. A page
    // opening is what makes a Downloaded module an APP here -- there is no
    // manifest for one to read a type off -- and an over-budget module with no
    // headless document is unloaded through the core, which is what the
    // container announces rather than does.
    host.backend()->watchWebContainer();
    // WHAT THIS BUILD SHIPS BESIDE THE MANIFEST. The app's own `web-modules`
    // tree is discovered by the core exactly as an installed package is -- same
    // scan, same shape -- so without this every shipped `web` module would read
    // as one the user downloaded.
    if (!shippedWebModulesDir.isEmpty()) {
        host.backend()->setShippedModules(
            QDir(shippedWebModulesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                 QDir::Name));
    }

    // The App Manager. AFTER the set is loaded, because everything it does
    // depends on which package modules the build carries, and it reports that
    // rather than failing: a Store shell's Bundled set is data (ADR 0007) and
    // the smallest useful one is chat and nothing else.
    //
    // The directories are the app's own writable data, which is the only place a
    // phone lets it install anything -- there is no shared modules directory
    // (ADR 0003).
    //
    // The SAME directories the core was started with. They used to be written
    // out here as `<data>/modules` and `<data>/ui-plugins`, which the core never
    // scanned: an install succeeded, reported a path, and the module never
    // appeared.
    host.backend()->startAppManager(moduleDirs);

    QMainWindow window;
    QWidget* shellWidget = shell->createShell(&host);
    window.setCentralWidget(shellWidget);
    host.replaySection();
    window.showFullScreen();
    console(QStringLiteral("COLD START: Shell shown at %1 ms").arg(sinceMain.elapsed()));

    // ── the two things the Shell is driven through, in the order a user
    // meets them ─────────────────────────────────────────────────────────
    //
    // Chat FIRST, and that ordering is the measurement: "cold start to Chat
    // usable" is how long the user waits before they can type, and putting
    // the Modules tab's own settle in front of it would report that delay as
    // part of the chat bring-up.
    //
    // Both run off the event loop rather than before it: a QML item has no
    // geometry until the first frame, and the chat bring-up spends its wait
    // in nested event loops, so the Shell stays on screen and painting
    // throughout.
    auto* network = new NetworkSmokeRunner(&core, &app);
    QObject::connect(network, &NetworkSmokeRunner::log, &console);
    QObject::connect(network, &NetworkSmokeRunner::chatUsable, &app, [&sinceMain]() {
        console(QStringLiteral("COLD START: Chat usable at %1 ms").arg(sinceMain.elapsed()));
    });

    auto* driver = new ShellModulesDriver(&host, shellWidget, &app);
    QObject::connect(driver, &ShellModulesDriver::log, &console);

    // The app in the Bundled set, opened from the sidebar. BEFORE the Modules
    // tab, and in that order for two reasons that are both about what the
    // other driver does: it unloads and reloads a core module, which resets
    // whatever the chat run left in it, and an app mounted on top of a module
    // that goes away is not a state worth asserting about. It is also the
    // order a user meets them in -- open the app, then go look at Settings.
    auto* apps = new ShellAppDriver(&host, shellWidget, &app);
    QObject::connect(apps, &ShellAppDriver::log, &console);

    // AND THE KEYBOARD, for the app's own input fields. Straight after the app
    // driver, because it needs the app on screen and it must not run behind the
    // web-app driver: opening a page hands the workspace to a platform view and
    // the question here is about the Shell's own focus chain.
    auto* keyboard = new ShellKeyboardDriver(&host, shellWidget, &app);
    QObject::connect(keyboard, &ShellKeyboardDriver::log, &console);

    // AND THE WEB APP, opened and LEFT AGAIN. A `web` module's UI is a platform
    // page rather than a widget, and the tile-press check above is satisfied by
    // a page that covers the Shell whole -- which is how a user ended up inside
    // an app with no way out (#110). This one asserts the other half: the page
    // is inset to the workspace, the chrome around it is live, and leaving the
    // app really puts the Shell back.
    auto* webApps = new ShellWebAppDriver(&host, shellWidget, &app);
    QObject::connect(webApps, &ShellWebAppDriver::log, &console);

    // THE CATALOG, if this launch was pointed at one. It has work only when the
    // command line named a repository (`--repository`, `--trust-signer`,
    // `--install`), which is a developer's run against a local catalog release
    // -- so it never appears in the path a cold-start measurement takes, and
    // running it FIRST when it does appear keeps the install out of the middle
    // of the chat bring-up it would otherwise be timed inside.
    auto* catalog = new ShellCatalogDriver(
        &host, shellWidget, basecamp::appmanager::CatalogSource::fromArguments(app.arguments()),
        &app);
    QObject::connect(catalog, &ShellCatalogDriver::log, &console);

    // CALLING A MODULE THAT HAS NO UI. `--call <module>.<method>(<args>)`, the
    // on-device `logoscore call` -- the only way to reach a `core` module on a
    // phone, where there is no second process to reach it from. Like the
    // catalog it has work only when the command line asked for some, so it
    // never appears in the path a cold-start measurement takes.
    auto* calls = new ShellCallDriver(
        &core, basecamp::appmanager::ModuleCallScript::fromArguments(app.arguments()), &app);
    QObject::connect(calls, &ShellCallDriver::log, &console);

    // THE 4.7.3 CONSENT PROMPT, ANSWERED. `--consent <caller>.<target>=deny,grant`
    // on the launch that installs, `=expect-granted` on the next one -- the
    // grant lives in capability_module's on-disk store and a second launch of
    // this app is the only thing that reads it. Constructed HERE, before any
    // driver runs, because it listens to the backend's console from this point
    // on: the Downloaded module makes its first call while its page comes up,
    // which is well before this driver's turn.
    auto* consent = new ShellConsentDriver(
        host.backend(), basecamp::appmanager::ConsentScript::fromArguments(app.arguments()),
        &app);
    QObject::connect(consent, &ShellConsentDriver::log, &console);
    // NOT a third COLD START marker: this clock starts at the tile press, and
    // the press happens after the chat bring-up has spent a minute and a half
    // waiting for the group to commit. Timed from main() it would read as a
    // four-minute app launch, which is a measurement of the peer's commit
    // latency wearing the app's name.
    QObject::connect(apps, &ShellAppDriver::appShown, &app,
                     [](const QString& name, qint64 elapsedMs) {
                         console(QStringLiteral("APP SHOWN: %1 on screen %2 ms after the "
                                                "sidebar tile was pressed")
                                     .arg(name).arg(elapsedMs));
                     });

    // Where the run LEAVES the user: on the app, not on the Settings page the
    // last check happened to end on. It is also what makes a screen recording
    // of the run worth anything -- the Modules tab's pass is three console
    // lines, and the app being on screen is the thing you would want to see.
    auto finishOnTheApp = [&host, apps, catalog]() {
        if (apps->hasWork() || catalog->hasWork())
            host.setCurrentSectionIndex(ShellSection::Workspace);
        // AND THE LAST THING OF ALL, after every driver: opening a catalog
        // row's links hands a URL to the platform, which puts a browser over
        // the Shell and stops turning its event loop. A driver sequenced behind
        // it would be waiting on a suspended process. It settles on the app
        // first, so the workspace above is what a recording of the run shows.
        catalog->openLinks();
    };

    QTimer::singleShot(0, &app, [network, driver, apps, keyboard, webApps, catalog,
                                 calls, consent, finishOnTheApp]() {
        // The catalog FIRST when there is one: the module it installs is what
        // the Modules tab and the sidebar then have to account for, and a run
        // pointed at a catalog is a developer's rather than a cold-start
        // measurement.
        if (catalog->hasWork()) {
            catalog->configure();
            catalog->run();
        }
        // AND THE CALLS AFTER IT, because the module a call names may be the one
        // the catalog just installed -- and BEFORE the chat bring-up, which
        // spends a minute and a half waiting for a group to commit and would
        // put that between a device and its answer.
        calls->run();
        // AND THE CONSENT ANSWERS AFTER THEM. The pair being decided is usually
        // the module the catalog just installed, and its first call is refused
        // while the page is still coming up -- so this waits on a page that has
        // already started rather than on one that has not.
        consent->run();
        if (network->hasWork()) {
            const bool ok = network->run();
            console(ok ? QStringLiteral("networking modules: PASS")
                       : QStringLiteral("networking modules: FAIL"));
            // The chat bring-up has just spent seconds turning the event
            // loop, so the scene has had far more than the one frame the
            // driver needs.
            // The CHAT half is what the app has to show, not the run's overall
            // verdict: the libp2p leg can fail on its own (an unanswered
            // local-network prompt on a device) with the group exchange
            // perfectly fine.
            apps->run(network->madeConversation());
            if (keyboard->hasWork()) keyboard->run();
            if (webApps->hasWork()) webApps->run();
            driver->run();
            finishOnTheApp();
        } else {
            console(QStringLiteral("networking modules: none in this Bundled set"));
            // Nothing ran ahead of it, so the tab needs its own settle: a QML
            // item has no geometry until the scene has painted, and a press
            // at the centre of a zero-sized button lands on nothing.
            QTimer::singleShot(2500, driver, [driver, apps, keyboard, webApps,
                                              finishOnTheApp]() {
                apps->run();
                if (keyboard->hasWork()) keyboard->run();
                if (webApps->hasWork()) webApps->run();
                driver->run();
                finishOnTheApp();
            });
        }
    });

    auto shutdown = [&]() {
        runner.stop();
        console(QStringLiteral("exiting"));
#if defined(Q_OS_IOS)
        // See mobile/liblogos-smoke/src/main.cpp: a fully static iOS image
        // faults in its own teardown after main() returns, well after the
        // core is down. Exit where the shutdown that matters has just been
        // logged, rather than crash a few frames later.
        std::fflush(nullptr);
        std::_Exit(0);
#else
        app.quit();
#endif
    };

    if (::pipe(g_signalPipe) == 0) {
        auto* notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, [&, notifier]() {
            notifier->setEnabled(false);
            char b;
            ssize_t ignored = ::read(g_signalPipe[0], &b, 1);
            (void)ignored;
            console(QStringLiteral("SIGTERM/SIGINT: shutting down"));
            shutdown();
        });
        std::signal(SIGTERM, onTerminate);
        std::signal(SIGINT, onTerminate);
    }

    const int rc = app.exec();
    console(QStringLiteral("event loop returned %1").arg(rc));
    shell->destroyShell(window.centralWidget());
    return rc;
}
