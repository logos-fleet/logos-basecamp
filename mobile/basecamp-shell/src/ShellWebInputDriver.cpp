#include "ShellWebInputDriver.h"

#include "BundledSetShellHost.h"
#include "ShellSections.h"
#include "WebAppSurface.h"
#include "webview/MobileWebContainerBackend.h"
#include "webview/WebPageInput.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QVariant>

using basecamp::shell::WebDriveFlow;
using basecamp::shell::WebDriveFlows;
using basecamp::shell::WebDriveStep;
using basecamp::shell::WebPageWatcher;
using basecamp::web::MobileWebContainerBackend;
using basecamp::web::WebPageInput;

namespace {

// How long the press on the tile gets to put a page on screen. The module may
// have to be loaded first, and that is a wasm image plus the app's 26 MB QML
// runtime -- the same budget ShellWebAppDriver gives the same press.
constexpr int kOpenBudgetMs = 60000;
// How long one call into the page gets to answer. The script waits ~300 ms for
// the accessibility tree and a typed call another ~1.2 s for the keys and the
// readback, and the page is sharing a thread with a QML scene.
constexpr int kAnswerBudgetMs = 15000;
// What a page gets to finish painting what the last step did, so a screenshot
// of the run shows the app in the state the flow left it.
constexpr int kHoldMs = 2500;

} // namespace

ShellWebInputDriver::ShellWebInputDriver(BundledSetShellHost* host, QWidget* shellWidget,
                                         QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
    , m_host(host)
{
    // FROM CONSTRUCTION, not from run(). A page answers on the container's own
    // signal and there is nothing to connect to a call; collecting from here
    // also means the lines a page printed while it was coming up are already in
    // hand when the first step asks about them.
    connect(MobileWebContainerBackend::instance(), &MobileWebContainerBackend::pageLog,
            this, [this](const QString&, const QString&, const QString& message) {
                m_pageLines.append(message);
            });
}

QString ShellWebInputDriver::appFor(const QString& flowName) const
{
    ShellModulesBackend* backend = m_host->backend();
    for (const QVariant& tile : backend->launcherApps()) {
        const QString name = tile.toMap().value(QStringLiteral("name")).toString();
        if (backend->isWebContainerApp(name)
            && !WebDriveFlows::select(name, flowName).isEmpty())
            return name;
    }
    return {};
}

bool ShellWebInputDriver::hasWork() const
{
    return !appFor(QString()).isEmpty();
}

bool ShellWebInputDriver::pageIsFrontmost(const QString& app) const
{
    WebAppSurface* surface = m_host->webSurface(app);
    return surface && surface->onScreen()
           && MobileWebContainerBackend::instance()->frontmostModule() == app;
}

bool ShellWebInputDriver::openApp(const QString& app)
{
    // ALREADY OPEN IS OPEN. Two flows on one app run back to back, and a second
    // press of the tile would be a press on the app that is already frontmost.
    if (pageIsFrontmost(app)) return true;

    m_host->setCurrentSectionIndex(ShellSection::Workspace);
    settle(200);
    QQuickItem* tile = waitFor(QStringLiteral("sidebar.app.%1").arg(app), 10000);
    if (!tile) {
        dumpNames(QStringLiteral("no sidebar tile for the web app '%1'").arg(app));
        return false;
    }
    scrollIntoView(tile);
    if (!tap(tile)) return false;

    QElapsedTimer sincePress;
    sincePress.start();
    while (sincePress.elapsed() < kOpenBudgetMs) {
        if (pageIsFrontmost(app)) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    emit log(QStringLiteral("WRONG: pressing %1's tile did not put its page on screen")
                 .arg(app));
    return false;
}

bool ShellWebInputDriver::send(const QString& app, const QString& call)
{
    // THE DRIVER, THEN THE CALL. The script installs itself once and returns
    // immediately when the page already has it, so every step can send it
    // rather than keeping track of which page has been prepared.
    auto* web = MobileWebContainerBackend::instance();
    if (web->runJavaScriptIn(app, WebPageInput::driverScript())
        && web->runJavaScriptIn(app, call))
        return true;
    emit log(QStringLiteral("WRONG: this platform cannot put a script in %1's page").arg(app));
    return false;
}

bool ShellWebInputDriver::waitForLine(const std::function<bool(const QString&)>& answered,
                                      int budgetMs, int& from)
{
    // Every line, offered exactly once: the predicates record what they read,
    // so a line seen twice would be read twice. `from` is the cursor itself, so
    // it is left past whatever was consumed however this returns.
    QElapsedTimer waiting;
    waiting.start();
    for (;;) {
        while (from < m_pageLines.size()) {
            if (answered(m_pageLines.at(from++)))
                return true;
        }
        if (waiting.elapsed() >= budgetMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

bool ShellWebInputDriver::ask(const QString& app, const QString& call,
                              const std::function<bool(const QString&)>& answered,
                              int budgetMs, int& from)
{
    return send(app, call) && waitForLine(answered, budgetMs, from);
}

bool ShellWebInputDriver::watch(WebPageWatcher& watcher, int budgetMs, int& from)
{
    // NOTHING IS SENT. The line this waits for is one the MODULE decided to
    // print -- see WebDriveFlows.h -- and it may already be in hand: `from` is
    // where the step that caused it started, not where this one did.
    return waitForLine([&watcher](const QString& line) { return watcher.offer(line); },
                       budgetMs, from);
}

void ShellWebInputDriver::run(const QStringList& flowNames)
{
    // A run that named no flow gets the app's first, which is what a plain
    // `--drive web-input` has always got.
    QStringList wanted = flowNames;
    if (wanted.isEmpty())
        wanted << QString();

    for (const QString& name : wanted) {
        const QString app = appFor(name);
        if (!app.isEmpty()) {
            walk(WebDriveFlows::select(app, name));
            continue;
        }
        if (name.isEmpty()) {
            emit log(QStringLiteral("web input: no `web` app in this build has a flow "
                                    "this driver knows"));
            continue;
        }
        // NAMED, AND WITH WHAT THERE IS. A flow this build has no app for and a
        // flow name that does not exist are the same blank result to a reader
        // and two different mistakes, so the tiles' own flows are on the line.
        const QStringList offered = offeredFlows();
        emit log(QStringLiteral("WRONG: no `web` app in this build has a flow called "
                                "'%1' -- this build offers: %2")
                     .arg(name, offered.isEmpty() ? QStringLiteral("(nothing)")
                                                  : offered.join(QLatin1String(", "))));
    }
}

QStringList ShellWebInputDriver::offeredFlows() const
{
    QStringList offered;
    for (const QVariant& tile : m_host->backend()->launcherApps()) {
        const QString name = tile.toMap().value(QStringLiteral("name")).toString();
        for (const QString& flow : WebDriveFlows::namesFor(name))
            offered << QStringLiteral("%1:%2").arg(name, flow);
    }
    return offered;
}

bool ShellWebInputDriver::walk(const WebDriveFlow& flow)
{
    emit log(QStringLiteral("web input: walking %1's '%2' flow").arg(flow.app, flow.name));
    if (!openApp(flow.app)) return false;

    // WHERE THE NEXT STEP STARTS READING. One cursor for the whole flow: a step
    // reads the lines the step before it left, and nothing is read twice.
    const int flowStart = m_pageLines.size();
    int cursor = flowStart;

    // WHAT THE PAGE OFFERS, BEFORE ANYTHING IS PRESSED. It is the line that
    // separates the two failures below from each other: a step that finds no
    // control is either a module naming its controls differently or an
    // accessibility tree that never woke up, and only this says which.
    if (!ask(flow.app, WebPageInput::describeCall(),
             [](const QString& line) {
                 return line.contains(WebPageInput::marker())
                        && (line.contains(QLatin1String("control(s)"))
                            || line.contains(QLatin1String("tree is empty")));
             },
             kAnswerBudgetMs, cursor)) {
        emit log(QStringLiteral("WRONG: %1's page never answered a script -- the page is "
                                "up and nothing in it is listening").arg(flow.app));
        return false;
    }

    for (const WebDriveStep& step : flow.steps) {
        if (step.act == WebDriveStep::Act::Await) {
            // AN `Await` READS FROM WHERE THE STEP BEFORE IT STARTED. A module
            // can publish its answer before the page has reported the press
            // that caused it, and a watch that began after the press would miss
            // it.
            WebPageWatcher watcher(step.watch);
            int watchFrom = step.watch.overTheWholeFlow ? flowStart : cursor;
            if (!watch(watcher, step.watch.budgetMs, watchFrom)) {
                emit log(watcher.linesSeen() == 0
                             ? QStringLiteral("WRONG: %1 never published a '%2' line, so there "
                                              "is no %3 -- see the page lines above")
                                   .arg(flow.app, step.watch.marker, step.watch.what)
                             : QStringLiteral("WRONG: %1 published %2 '%3' line(s) and %4 never "
                                              "arrived (%5 stayed at '%6')")
                                   .arg(flow.app)
                                   .arg(watcher.linesSeen())
                                   .arg(step.watch.marker, step.watch.what, step.watch.field,
                                        watcher.baseline()));
                return false;
            }
            // A whole-flow watch is a SCAN of what already happened, so it
            // must not drag the cursor back over lines the steps after it will
            // read.
            if (!step.watch.overTheWholeFlow)
                cursor = watchFrom;
            emit log(QStringLiteral("web input: %1 published %2 -- %3 = %4")
                         .arg(flow.app, step.watch.what, step.watch.field, watcher.reading()));
            continue;
        }

        // A PRESS THAT IS NOT WAITED FOR. Sent and left in the page, because
        // the step after it has to reach the page before the host stops
        // answering -- see WebDriveFlows.h. The cursor is untouched: what the
        // press did is read by the `Await` steps that follow it.
        if (step.act == WebDriveStep::Act::PressAhead) {
            if (!send(flow.app, WebPageInput::pressCall(step.control, step.afterMs)))
                return false;
            if (step.afterMs > 0)
                emit log(QStringLiteral("web input: armed %1's '%2' press, +%3 ms on the "
                                        "page's own clock")
                             .arg(flow.app, step.control).arg(step.afterMs));
            else
                emit log(QStringLiteral("web input: sent %1's '%2' press without waiting "
                                        "for the page to confirm it")
                             .arg(flow.app, step.control));
            continue;
        }

        // Press, Type or Read: one call into the page, and one answer waited
        // for. The other two acts have gone their own way above.
        const bool typing = step.act == WebDriveStep::Act::Type;
        const bool reading = step.act == WebDriveStep::Act::Read;
        QString call;
        if (typing)
            call = WebPageInput::typeCall(step.control, step.text);
        else if (reading)
            call = WebPageInput::readCall(step.control);
        else
            call = WebPageInput::pressCall(step.control);
        // A refusal ENDS the wait as surely as the answer does, and the two are
        // told apart afterwards: waiting a whole budget out for a control the
        // page has already said it does not have proves nothing and costs 15 s.
        bool refused = false;
        QString held;
        const auto done = [&](const QString& line) {
            refused = WebPageInput::refusalReported(line, step.control);
            if (refused) return true;
            if (typing || reading) {
                const auto value = WebPageInput::valueReported(line, step.control);
                if (!value) return false;
                held = *value;
                return true;
            }
            return WebPageInput::pressReported(line, step.control);
        };
        if (!ask(flow.app, call, done, kAnswerBudgetMs, cursor)) {
            emit log(QStringLiteral("WRONG: %1's page said nothing about '%2' -- see the "
                                    "`logos-drive:` lines above").arg(flow.app, step.control));
            return false;
        }
        if (refused) {
            emit log(QStringLiteral("WRONG: %1's page has no reachable control called "
                                    "'%2' -- the names it does have are on the line above")
                         .arg(flow.app, step.control));
            return false;
        }
        // A READ IS NOT ALWAYS A READBACK OF SOMETHING TYPED. `seed-import` reads
        // the field it filled, where a mismatch means the keys never landed;
        // `proxy-config` reads a status line the module wrote, where it means
        // the module said something else. So this states what was required and
        // what is there, and leaves the cause to the lines above it.
        if (reading && held != step.text) {
            emit log(QStringLiteral("WRONG: %1's '%2' was required to hold '%3' and holds "
                                    "'%4'")
                         .arg(flow.app, step.control, step.text, held));
            return false;
        }
        if (typing)
            emit log(QStringLiteral("web input: typed %1 character(s) into %2's '%3'")
                         .arg(step.text.size()).arg(flow.app, step.control));
        else if (reading)
            emit log(QStringLiteral("web input: %1's '%2' holds '%3' -- the module's own "
                                    "state, read back off the page")
                         .arg(flow.app, step.control, held));
        else
            emit log(QStringLiteral("web input: pressed %1's '%2'").arg(flow.app, step.control));
    }

    emit log(QStringLiteral("%1: %2's '%3' flow, every step from inside the app")
                 .arg(flow.verdict, flow.app, flow.name));
    // HELD, so a screen recording of the run shows what the flow left rather
    // than whatever the next pass navigates to.
    settle(kHoldMs);
    return true;
}
