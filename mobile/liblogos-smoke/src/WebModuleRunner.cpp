#include "WebModuleRunner.h"

#include "webview/AppMemory.h"
#include "webview/MobileWebContainerBackend.h"

#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>
#include <QTimer>

using basecamp::web::megabytes;
using basecamp::web::MobileWebContainerBackend;

WebModuleRunner::WebModuleRunner(ICoreRuntime* core, QString webModulesDir, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_webModulesDir(std::move(webModulesDir))
{
    connect(MobileWebContainerBackend::instance(), &MobileWebContainerBackend::pageLog,
            this, [this](const QString& module, const QString& level, const QString& message) {
                m_pageLines.append(message);
                // Only what the MODULE says, not every line a 25 MB runtime
                // emits while booting: the log is read off a device console and
                // the interesting lines would be lost in it.
                // `contains`, not `startsWith`: Qt's own console route prefixes
                // a page's qml log with "qml: ".
                if (message.contains(QStringLiteral("logos-view:"))
                    || message.contains(QStringLiteral("[logos-web-view"))
                    || level == QLatin1String("error"))
                    emit log(QStringLiteral("web %1: %2").arg(module, message));
            });
}

void WebModuleRunner::pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool WebModuleRunner::waitForPageLine(const QString& pattern, int timeoutMs)
{
    const QRegularExpression re(pattern);
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        for (const QString& line : m_pageLines)
            if (re.match(line).hasMatch()) return true;
        if (timer.elapsed() >= timeoutMs) return false;
        pump(100);
    }
}

QStringList WebModuleRunner::available() const
{
    if (m_webModulesDir.isEmpty()) return {};
    const QStringList known = m_core->knownModules();
    QStringList out;
    for (const QString& name :
         QDir(m_webModulesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (known.contains(name)) out.append(name);
    }
    return out;
}

qint64 WebModuleRunner::bringUp(const QString& name)
{
    auto* backend = MobileWebContainerBackend::instance();

    QElapsedTimer timer;
    timer.start();
    // THE CONTAINER'S OWN VERDICT. liblogos' WebContainer returns from a load
    // only once it has asked the page for its interface and the page has
    // answered, so a true here already means both wasm images booted and the
    // module is published.
    if (!m_core->loadModule(name)) {
        emit log(QStringLiteral("web module %1: the core did not load it").arg(name));
        return -1;
    }
    emit log(QStringLiteral("web module %1: loaded and published in %2 ms")
                 .arg(name).arg(timer.elapsed()));

    // The shell says what the user is looking at. This is what brings the page
    // in front of Qt's own surface and what charges it against the budget.
    backend->show(name);
    if (!backend->hasView(name)) {
        emit log(QStringLiteral("web module %1: the container opened no page").arg(name));
        return -1;
    }

    // THE VIEW'S OWN REPORT. `logos-view: ready <name> count=0` is the module's
    // QML saying it took its backend replica over the MessagePort -- the fact a
    // screenshot of a canvas cannot establish.
    if (!waitForPageLine(QStringLiteral("logos-view: ready %1 count=").arg(name), 90000)) {
        emit log(QStringLiteral("web module %1: the view never reported a backend").arg(name));
        return -1;
    }
    return timer.elapsed();
}

bool WebModuleRunner::run()
{
    auto* backend = MobileWebContainerBackend::instance();
    const QStringList modules = available();

    if (modules.isEmpty()) {
        emit log(m_webModulesDir.isEmpty()
                     ? QStringLiteral("web modules: this build ships none")
                     : QStringLiteral("web modules: none discovered under %1")
                           .arg(m_webModulesDir));
        return true;
    }
    emit log(QStringLiteral("web modules: %1 (from %2)")
                 .arg(modules.join(QStringLiteral(", ")), m_webModulesDir));
    emit log(MobileWebContainerBackend::appMemoryLine(QStringLiteral("before any web module")));

    // ── the first module's UI, cold ────────────────────────────────────────
    const QString first = modules.first();
    const qint64 coldMs = bringUp(first);
    if (coldMs < 0) return false;
    // AGAINST THE SPIKE'S BASELINE, which is the number slice 28 asks this to
    // be logged against: 2.6-3 s for a Qt-wasm QML runtime's first paint,
    // measured in a desktop browser.
    emit log(QStringLiteral("COLD START: %1's UI ready at %2 ms "
                            "(spike baseline 2600-3000 ms)")
                 .arg(first).arg(coldMs));

    // Laid out, not merely alive: the fixture reports where its button is in
    // window coordinates, and a view that never got geometry reports 0 0.
    const bool laidOut = waitForPageLine(
        QStringLiteral("logos-view: button-at ([1-9][0-9]*) ([1-9][0-9]*)"), 20000);
    emit log(laidOut
                 ? QStringLiteral("web module %1: its view is laid out inside the page").arg(first)
                 : QStringLiteral("web module %1: its view never reported a button position")
                       .arg(first));

    if (modules.size() < 2) {
        emit log(QStringLiteral("live-runtime budget: only one web module is installed, "
                                "so there is nothing to evict"));
        return laidOut;
    }

    // ── the second module, and the budget ──────────────────────────────────
    const QString second = modules.at(1);
    QStringList evicted;
    const auto evictConnection =
        connect(backend, &MobileWebContainerBackend::uiEvictionRequired, this,
                [&evicted](const QString& name) { evicted.append(name); });

    const qint64 secondMs = bringUp(second);
    disconnect(evictConnection);
    if (secondMs < 0) return false;
    emit log(QStringLiteral("COLD START: %1's UI ready at %2 ms").arg(second).arg(secondMs));

    if (!evicted.contains(first)) {
        emit log(QStringLiteral("WRONG: showing %1 did not put %2 over the live-runtime budget")
                     .arg(second, first));
        return false;
    }

    // ANSWERING THE EVICTION IS THE HOST'S JOB, and it is done through the
    // core: the page belongs to liblogos' container, which destroys it when the
    // module unloads. A shell that tore the view down itself would leave a
    // published module with a dead channel.
    const qint64 held = basecamp::web::appResidentBytes();
    for (const QString& name : evicted) m_core->unloadModule(name, /*withDependents=*/false);
    // The webview's own teardown is asynchronous on both platforms, and what is
    // reclaimed is reclaimed by the OS afterwards. Two seconds is what the
    // number is worth reading after.
    pump(2000);
    const qint64 after = basecamp::web::appResidentBytes();

    const bool released = !backend->hasView(first);
    emit log(released
                 ? QStringLiteral("live-runtime budget: %1 gave up its UI page for %2")
                       .arg(first, second)
                 : QStringLiteral("WRONG: %1 still holds a page after being unloaded").arg(first));
    emit log(QStringLiteral("live-runtime budget: %1 of %2 held by %3 live runtime(s)")
                 .arg(megabytes(backend->budget().projectedBytes()),
                      megabytes(backend->budget().budgetBytes()),
                      QString::number(backend->budget().live().size())));
    // THE HOST'S OWN FOOTPRINT, AND WHAT IT DOES NOT INCLUDE. Both phones run a
    // webview's content in a process of their own -- WebKit's WebContent,
    // Chromium's sandboxed renderer -- and an embedder cannot weigh either: iOS
    // offers no API for another task's footprint and Android's renderer runs
    // under a different uid, so its /proc is not ours to read. So the page's
    // 185-240 MB is NOT in these numbers, and saying so beats printing a
    // difference of zero as though the runtime cost nothing.
    //
    // What a shell CONTROLS is how many pages are alive, which is the line
    // above; this one is what the app itself holds either side of an eviction.
    emit log(QStringLiteral("app footprint %1 -> %2 (the evicted page's own cost is in "
                            "the platform's WebContent/renderer process, which an "
                            "embedder cannot weigh)")
                 .arg(megabytes(held), megabytes(after)));

    // ── and the evicted module is still there, as a module ─────────────────
    // KNOWN LIMIT, REPORTED AS ONE. A `ui_qml` module's `web` variant today is
    // ONE page carrying both the QML runtime and its own Qt-wasm backend image
    // (logos-module-builder's buildWebViewModule.nix), so giving up the UI gives
    // up the Wasm host with it. Slice 28 asks for a background module that keeps
    // answering, and that needs the variant to ship a second, headless entry
    // document. Nothing in the container changes when it does -- the budget
    // already governs UI pages only -- so what is checked here is the half that
    // IS true: the module is still installed and can be brought back.
    const bool stillKnown = m_core->knownModules().contains(first);
    emit log(stillKnown
                 ? QStringLiteral("web module %1: still installed with its UI evicted; its Wasm "
                                  "host went with the page (one entry document per variant)")
                       .arg(first)
                 : QStringLiteral("WRONG: %1 disappeared from the installed set").arg(first));

    return laidOut && released && stillKnown;
}
