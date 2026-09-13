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
#include "appmanager/ModuleDirectories.h"
#include "IShellHost.h"
#include "IShellView.h"
#include "NetworkSmokeRunner.h"
#include "PlatformConsole.h"
#include "ShellAppDriver.h"
#include "ShellModulesDriver.h"
#include "ShellSections.h"
#include "SmokeRunner.h"

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
    const basecamp::appmanager::ModuleDirectories moduleDirs =
        basecamp::appmanager::ModuleDirectories::under(appDataRoot);
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
    host.backend()->startAppManager(moduleDirs.installModulesDir,
                                    moduleDirs.installUiPluginsDir);

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
    auto backToTheApp = [&host, apps]() {
        if (apps->hasWork())
            host.setCurrentSectionIndex(ShellSection::Workspace);
    };

    QTimer::singleShot(0, &app, [network, driver, apps, backToTheApp]() {
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
            driver->run();
            backToTheApp();
        } else {
            console(QStringLiteral("networking modules: none in this Bundled set"));
            // Nothing ran ahead of it, so the tab needs its own settle: a QML
            // item has no geometry until the scene has painted, and a press
            // at the centre of a zero-sized button lands on nothing.
            QTimer::singleShot(2500, driver, [driver, apps, backToTheApp]() {
                apps->run();
                driver->run();
                backToTheApp();
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
