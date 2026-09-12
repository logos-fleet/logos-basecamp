// THE WEB CONTAINER, END TO END, IN BASECAMP'S OWN PROCESS.
//
// It loads a `ui_qml` module's `web` variant THROUGH THE REAL CORE — the same
// logos::host::LogosCore basecamp's QtLogosCoreRuntime owns — and then does to
// the resulting widget what a user does: it looks at it and clicks it.
//
//     variant on disk
//        -> the core's discovery stamps it `web`
//        -> WebContainer opens a view through basecamp's factory
//        -> this page serves the package and the bundled QML runtime on `logos:`
//        -> the runtime boots, the backend image boots, QtRO joins them
//        -> a REAL Qt mouse press on the widget drives the module's button
//        -> the backend's property change comes back and the view redraws
//        -> `logos.callModuleAsync` leaves the page as a logos-protocol Call,
//           crosses the container's channel and reaches a NATIVE module
//
// NOT A UNIT TEST AND NOT A BROWSER HARNESS. logos-module-builder's
// wasm/browser-e2e proves the variant in Chrome against a stub container; this
// proves the CONTAINER — basecamp's scheme, basecamp's channel, basecamp's
// widget, the real core behind it — and it is the only thing that can.
//
// WHAT IT READS TO KNOW WHAT HAPPENED: the page's console. A view draws into a
// canvas; from outside there is no DOM to query and no label to read. The
// fixture variant therefore logs what it did, basecamp's page routes the
// console into Qt's message handler (see WebModulePageView's LoggingPage), and
// this installs a handler to collect it. That is not a shortcut around the
// assertion — the lines are emitted by the module's own QML reacting to its own
// replica, which is exactly the fact under test.

#include "web/LogosWebScheme.h"
#include "web/WebContainerBackend.h"

// THE C API, not logos-cpp-sdk's logos_host_core.h veneer. That header mirrors
// liblogos' `LogosLoadDeps` rather than including it (it cannot: liblogos
// depends on the SDK, so including the other way round would invert the graph),
// and web_module_view.h pulls in logos_core.h for LOGOS_CORE_EXPORT — so a
// translation unit that wants BOTH the webview seam and the host veneer gets
// the enum twice and does not compile. A driver needs six calls; it takes them
// from the one header that also declares the seam it is filling.
#include <logos_core.h>

#include <QtWebEngineQuick/QtWebEngineQuick>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMutex>
#include <QMutexLocker>
#include <QPoint>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <vector>

namespace {

QMutex gLogMutex;
QStringList gLogLines;
QtMessageHandler gPreviousHandler = nullptr;

void collectingMessageHandler(QtMsgType type, const QMessageLogContext& context,
                              const QString& message)
{
    {
        QMutexLocker lock(&gLogMutex);
        gLogLines.append(message);
    }
    if (gPreviousHandler) gPreviousHandler(type, context, message);
}

QStringList logSnapshot()
{
    QMutexLocker lock(&gLogMutex);
    return gLogLines;
}

struct Check {
    QString name;
    bool ok = false;
    QString detail;
};

std::vector<Check> gChecks;

void check(const QString& name, bool ok, const QString& detail = {})
{
    gChecks.push_back({ name, ok, detail });
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL",
           name.toUtf8().constData(),
           detail.isEmpty() ? "" : "  — ",
           detail.toUtf8().constData());
    fflush(stdout);
}

// Pump the Qt event loop for `ms`. Everything here waits this way rather than
// sleeping: the page delivers on this thread, so a sleeping test is a test that
// prevents the very thing it is waiting for.
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// The last line matching `pattern` at or after `since`, waiting up to
// `timeoutMs` for one to appear.
//
// `since` is not bookkeeping: the view logs its count on EVERY change including
// the first binding, so "wait for a count line" would be satisfied by the line
// that was already there before the click. Every wait for something this test
// just caused passes the log length it measured beforehand.
QRegularExpressionMatch waitForLog(const QString& pattern, int since, int timeoutMs)
{
    const QRegularExpression re(pattern);
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const QStringList lines = logSnapshot();
        for (int i = lines.size() - 1; i >= since; --i) {
            const QRegularExpressionMatch m = re.match(lines.at(i));
            if (m.hasMatch()) return m;
        }
        if (timer.elapsed() >= timeoutMs) return QRegularExpressionMatch();
        pump(100);
    }
}

int logCount()
{
    QMutexLocker lock(&gLogMutex);
    return gLogLines.size();
}

// A `char**` module list from the C API, as a QStringList, freed correctly.
// liblogos allocates these with `new char[]` / `new char*[]`, so `delete[]` is
// the right deallocator and free() would be undefined.
QStringList takeModuleNames(char** raw)
{
    QStringList names;
    if (!raw) return names;
    for (char** p = raw; *p; ++p) { names << QString::fromUtf8(*p); delete[] *p; }
    delete[] raw;
    return names;
}

} // namespace

int main(int argc, char** argv)
{
    // Before the QApplication, both of them: QtWebEngine requires its own
    // initialize() first, and a custom URL scheme registered after the
    // application exists is silently ignored — the failure mode is a page that
    // loads nothing, with no diagnostic anywhere.
    basecamp::web::registerLogosWebScheme();
    QtWebEngineQuick::initialize();

    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Logos"));
    app.setApplicationName(QStringLiteral("logos-basecamp-web-container-test"));

    QCommandLineParser parser;
    QCommandLineOption modulesOpt(QStringLiteral("modules-dir"),
                                  QStringLiteral("Directory holding the module packages."),
                                  QStringLiteral("path"));
    QCommandLineOption runtimeOpt(QStringLiteral("runtime-dir"),
                                  QStringLiteral("The bundled QML runtime's www directory."),
                                  QStringLiteral("path"));
    QCommandLineOption moduleOpt(QStringLiteral("module"),
                                 QStringLiteral("The web module to load."),
                                 QStringLiteral("name"));
    QCommandLineOption nativeOpt(QStringLiteral("native-module"),
                                 QStringLiteral("The native module the view calls."),
                                 QStringLiteral("name"));
    parser.addOption(modulesOpt);
    parser.addOption(runtimeOpt);
    parser.addOption(moduleOpt);
    parser.addOption(nativeOpt);
    parser.addHelpOption();
    parser.process(app);

    const QString modulesDir = parser.value(modulesOpt);
    const QString runtimeDir = parser.value(runtimeOpt);
    const QString moduleName = parser.value(moduleOpt);
    const QString nativeModule = parser.value(nativeOpt);
    if (modulesDir.isEmpty() || runtimeDir.isEmpty() || moduleName.isEmpty()) {
        fprintf(stderr, "usage: --modules-dir <dir> --runtime-dir <dir> --module <name>\n");
        return 2;
    }

    gPreviousHandler = qInstallMessageHandler(collectingMessageHandler);

    // ── the shell's half ──────────────────────────────────────────────────
    //
    // A plain window standing in for basecamp's workspace area. What matters is
    // that the widget the container produces is MOUNTED, SIZED and VISIBLE: a
    // module's view that is never shown never renders, never lays out, and
    // reports a button at 0,0.
    QWidget host;
    host.setWindowTitle(QStringLiteral("Web container"));
    auto* layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    host.resize(480, 640);
    host.show();

    QWidget* mounted = nullptr;
    QObject::connect(basecamp::web::WebContainerBackend::instance(),
                     &basecamp::web::WebContainerBackend::viewOpened,
                     [&](const QString& name, QWidget* widget) {
                         if (name != moduleName || !widget) return;
                         mounted = widget;
                         layout->addWidget(widget);
                         widget->show();
                     });

    basecamp::web::WebContainerBackend::instance()->install(runtimeDir);

    // ── the core ──────────────────────────────────────────────────────────
    logos_core_init(argc, argv);
    logos_core_add_modules_dir(modulesDir.toUtf8().constData());
    logos_core_start();

    const QStringList known = takeModuleNames(logos_core_get_known_modules());
    check(QStringLiteral("the core discovered the web variant as a module"),
          known.contains(moduleName), known.join(QStringLiteral(", ")));

    // ── what the page will call, brought up before the page ────────────────
    //
    // ORDER IS PART OF THE ASSERTION. The view calls `greeter` 400 ms after its
    // QML arrives, so a native module loaded after the page would make this
    // check's verdict depend on how fast a browser started.
    //
    // capability_module is not loaded here because the CORE loads its own broker
    // at start (initializeCapabilityModule), and it has to: the page's calls go
    // out as the module's own identity on a store that is born empty, so its
    // first call to any target runs `capability_module.requestModule`. With no
    // broker that call is refused at the target's empty-token check — which
    // looks exactly like a container that never routed anything. So it is
    // asserted rather than assumed.
    if (!nativeModule.isEmpty()) {
        const QStringList loaded = takeModuleNames(logos_core_get_loaded_modules());
        check(QStringLiteral("the core brought up the capability broker"),
              loaded.contains(QStringLiteral("capability_module")),
              loaded.join(QStringLiteral(", ")));

        const bool nativeLoaded =
            logos_core_load_module(nativeModule.toUtf8().constData(),
                                   LOGOS_LOAD_REQUIRED_DEPS) != 0;
        check(QStringLiteral("the native module the view calls is loaded"),
              nativeLoaded, nativeModule);
    }

    // THE CONTAINER'S OWN VERDICT. loadModule returns only once
    // WebContainer::awaitLoad has asked the page for its interface and the view
    // host has answered it — so a true here already means the page booted both
    // wasm images and is serving as a provider.
    const bool loaded = logos_core_load_module(moduleName.toUtf8().constData(),
                                              LOGOS_LOAD_REQUIRED_DEPS) != 0;
    check(QStringLiteral("the core loaded it through the Web container"), loaded);
    check(QStringLiteral("the container mounted a widget for it"), mounted != nullptr);

    if (!loaded || !mounted) {
        printf("\nFAIL: the page never came up; nothing below could be asserted\n");
        for (const QString& line : logSnapshot()) printf("      %s\n", line.toUtf8().constData());
        return 1;
    }

    // ── the view took its backend ─────────────────────────────────────────
    const QRegularExpressionMatch ready = waitForLog(
        QStringLiteral("logos-view: ready %1 count=(\\d+)").arg(moduleName), 0, 60000);
    check(QStringLiteral("the view took its backend replica over the MessagePort"),
          ready.hasMatch() && ready.captured(1) == QLatin1String("0"),
          ready.hasMatch() ? QStringLiteral("count=%1").arg(ready.captured(1))
                           : QStringLiteral("never became ready"));

    const QRegularExpressionMatch at =
        waitForLog(QStringLiteral("logos-view: button-at (\\d+) (\\d+)"), 0, 30000);
    check(QStringLiteral("the view is laid out inside the container's widget"),
          at.hasMatch() && at.captured(1).toInt() > 0 && at.captured(2).toInt() > 0,
          at.hasMatch() ? at.captured(1) + QStringLiteral(",") + at.captured(2)
                        : QStringLiteral("the button never reported a position"));

    // ── a REAL click, from the host's own input stack ──────────────────────
    //
    // Not a synthesised DOM event: a Qt mouse press on the widget basecamp
    // mounted, which Chromium turns into the page's pointer event, which Qt for
    // WebAssembly turns into the button's press. Every layer the user's click
    // goes through is in it.
    if (at.hasMatch()) {
        const int before = logCount();
        const QPoint target(at.captured(1).toInt(), at.captured(2).toInt());
        QWidget* input = mounted->focusProxy() ? mounted->focusProxy() : mounted;
        QTest::mouseMove(input, target);
        pump(150);
        QTest::mouseClick(input, Qt::LeftButton, Qt::NoModifier, target);

        const QRegularExpressionMatch changed = waitForLog(
            QStringLiteral("logos-view: changed %1 count=(\\d+)").arg(moduleName),
            before, 30000);
        // TWO FACTS, NOT ONE: the count the VIEW reports is the property change
        // coming BACK from the backend through the replica. A click that reached
        // the backend but whose change never returned would leave the view
        // showing 0, and that is the half a screenshot cannot tell apart.
        check(QStringLiteral("the click drove the backend and the change came back"),
              changed.hasMatch() && changed.captured(1) == QLatin1String("1"),
              changed.hasMatch() ? QStringLiteral("count=%1").arg(changed.captured(1))
                                 : QStringLiteral("the view never saw a change"));
    }

    // ── callModuleAsync reaches a native module ───────────────────────────
    if (!nativeModule.isEmpty()) {
        const QRegularExpressionMatch answered =
            waitForLog(QStringLiteral("logos-view: callModuleAsync -> (.+)"), 0, 30000);
        check(QStringLiteral("callModuleAsync from the view reached the native module"),
              answered.hasMatch() && answered.captured(1).contains(QStringLiteral("hello logos")),
              answered.hasMatch() ? answered.captured(1)
                                  : QStringLiteral("the callback never fired"));
    }

    // ── unloading takes the page down ─────────────────────────────────────
    bool closed = false;
    QObject::connect(basecamp::web::WebContainerBackend::instance(),
                     &basecamp::web::WebContainerBackend::viewClosed,
                     [&](const QString& name) { if (name == moduleName) closed = true; });
    logos_core_unload_module(moduleName.toUtf8().constData(), /*with_dependents=*/false);
    for (int i = 0; i < 50 && !closed; ++i) pump(100);
    check(QStringLiteral("unloading the module takes its page down"), closed);

    int failed = 0;
    for (const Check& c : gChecks) if (!c.ok) ++failed;
    if (failed) {
        printf("\n--- the page's console ---\n");
        for (const QString& line : logSnapshot()) printf("      %s\n", line.toUtf8().constData());
    }
    printf("\n%s: the Web container renders a ui_qml `web` variant (%d checks)\n",
           failed ? "FAIL" : "PASS", int(gChecks.size()));
    logos_core_cleanup();
    return failed ? 1 : 0;
}
