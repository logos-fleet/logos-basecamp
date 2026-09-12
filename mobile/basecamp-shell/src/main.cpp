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
#include "IShellHost.h"
#include "IShellView.h"
#include "NetworkSmokeRunner.h"
#include "ShellModulesDriver.h"
#include "SmokeRunner.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPluginLoader>
#include <QSocketNotifier>
#include <QTimer>

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
    std::fprintf(stderr, "[shell] %s\n", qUtf8Printable(line));
    std::fflush(stderr);
}

void qtMessages(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    // QML warnings are the interesting output of a shell that renders a
    // scene it has never rendered on this platform before, so they go to the
    // platform console like everything else.
    std::fprintf(stderr, "[qt] %s\n", qUtf8Printable(msg));
    std::fflush(stderr);
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
    BundledSetCoreRuntime core(runner.prepare(argc, argv));
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

    QMainWindow window;
    QWidget* shellWidget = shell->createShell(&host);
    window.setCentralWidget(shellWidget);
    host.replaySection();
    window.showFullScreen();
    console(QStringLiteral("shell on screen, %1 ms since main()").arg(sinceMain.elapsed()));
    console(QStringLiteral("COLD START: Shell shown at %1 ms").arg(sinceMain.elapsed()));

    // ── the two things the Shell is driven through, in the order a user
    // meets them ─────────────────────────────────────────────────────────
    //
    // Chat FIRST, and that ordering is the measurement: "cold start to Chat
    // usable" is how long the user waits before they can type, and putting
    // the Modules tab's own 2.5-second settle in front of it would report
    // that delay as part of the chat bring-up. The Modules tab is driven
    // after, when the scene has had several seconds of frames -- more than
    // the one it needs.
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

    QTimer::singleShot(0, &app, [network, driver]() {
        if (network->hasWork()) {
            const bool ok = network->run();
            console(ok ? QStringLiteral("networking modules: PASS")
                       : QStringLiteral("networking modules: FAIL"));
        } else {
            console(QStringLiteral("networking modules: none in this Bundled set"));
        }
        driver->run();
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
