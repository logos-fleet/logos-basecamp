// The liblogos smoke host: liblogos_core running on a phone, with nothing
// loaded. It starts the core against an empty modules directory and a
// persistence path inside the app sandbox, prints the modules listing and the
// protocol version to the screen and to the platform console, and stays alive
// until Quit is pressed -- at which point the core is cleaned up before the
// process exits, so a hung shutdown is visible as a hang and not as a kill.
#include <QtGlobal>

#include "BundledModuleRunner.h"
#include "SmokeRunner.h"
#include "ViewModuleRunner.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQuickWidget>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#if defined(Q_OS_ANDROID)
#include <android/log.h>
#endif

namespace {

QPlainTextEdit* g_log = nullptr;

void console(const char* tag, const QString& line)
{
#if defined(Q_OS_ANDROID)
    __android_log_print(ANDROID_LOG_INFO, "logos-smoke", "[%s] %s", tag, qUtf8Printable(line));
#else
    std::fprintf(stderr, "[%s] %s\n", tag, qUtf8Printable(line));
    std::fflush(stderr);
#endif
}

void say(const QString& line)
{
    console("smoke", line);
    if (g_log) g_log->appendPlainText(line);
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
    if (g_log && type >= QtWarningMsg) g_log->appendPlainText("qt: " + msg);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName("Logos");
    app.setApplicationName("LiblogosSmoke");
    qInstallMessageHandler(qtMessages);

    QMainWindow window;
    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    // The VIEW module's surface: a Qt Quick scene inside this Widgets host,
    // so the log below it stays on screen. The module's QML is loaded into
    // THIS engine -- the host's own -- which is the whole shape of a Bundled
    // view module on a phone.
    auto* viewSurface = new QQuickWidget;
    viewSurface->setResizeMode(QQuickWidget::SizeRootObjectToView);
    viewSurface->setMinimumHeight(240);
    auto* logView = new QPlainTextEdit;
    logView->setReadOnly(true);
    auto* quit = new QPushButton(QStringLiteral("Quit"));
    layout->addWidget(viewSurface, 2);
    layout->addWidget(logView, 1);
    layout->addWidget(quit, 0);
    g_log = logView;
    window.setCentralWidget(central);
    window.showFullScreen();

    SmokeRunner runner;
    QObject::connect(&runner, &SmokeRunner::log, &say);

    // Measured from main(), not from run(): on a phone the interesting number
    // is how long the user waits, and that includes QApplication and the
    // window. logos_core_start()'s own time is reported separately by run().
    QElapsedTimer sinceMain;
    sinceMain.start();
    runner.run(argc, argv);
    say(QStringLiteral("core up, %1 ms since main()").arg(sinceMain.elapsed()));

    // The app's ONE Bundled module, brought up in the Native container. It is
    // run after the core is up and before the event loop, so the verdict is on
    // screen and in the platform console by the time the first frame is drawn
    // -- an automated run reads it off the console and never has to tap
    // anything.
    BundledModuleRunner bundled;
    QObject::connect(&bundled, &BundledModuleRunner::log, &say);
    const bool bundledOk = bundled.run();
    say(bundledOk ? QStringLiteral("bundled module: PASS")
                  : QStringLiteral("bundled module: FAIL"));

    // ...and the app's ONE Bundled VIEW module, brought up the same way and
    // for the same reason: on screen and on the console before the first
    // frame, so an automated run reads the verdict off the console.
    ViewModuleRunner view;
    QObject::connect(&view, &ViewModuleRunner::log, &say);
    const bool viewOk = view.run(viewSurface);
    say(viewOk ? QStringLiteral("view module: PASS")
               : QStringLiteral("view module: FAIL"));

    // Then press the view's own button once, after the first frame -- the
    // scene has no geometry before it, so there is no button to press yet.
    // An automated run reads the round trip off the console; a human sees the
    // number change on screen and can press it again.
    if (viewOk)
        QTimer::singleShot(1500, &view, &ViewModuleRunner::driveViewOnce);

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
