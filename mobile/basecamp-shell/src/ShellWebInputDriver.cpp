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
// of the run shows the form holding what it was given.
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

ShellWebInputDriver::TypedFlow ShellWebInputDriver::flowFor(const QString& app)
{
    // wallet_ui's Advanced tab: the seed-phrase import, which is the flow
    // logos-workspace#147 could not verify on a device and the reason #174 was
    // split out of it.
    //
    // A CONTROL IS NAMED BY WHICHEVER HANDLE IT HAS. The buttons here are named
    // by their text, which is what Qt's accessibility tree publishes for one;
    // the FIELDS are named by their `objectName`, because Qt publishes a text
    // editor with no accessible name at all and the module already carries
    // objectNames for the desktop inspector to find it by. WebPageInput.h has
    // the account of the two handles.
    //
    // The seed is the all-zero BIP-39 test vector, deliberately: it is the
    // phrase every wallet test in this workspace uses, it is worthless, and it
    // must never be a phrase anyone could have funded.
    if (app == QLatin1String("wallet_ui")) {
        TypedFlow flow;
        flow.steps = {
            { QStringLiteral("Advanced"), QString() },
            { QStringLiteral("advSeedField"),
              QStringLiteral("abandon abandon abandon abandon abandon abandon abandon "
                             "abandon abandon abandon abandon about") },
            { QStringLiteral("advAcctLabelField"), QStringLiteral("issue174") },
            { QStringLiteral("advAcctPwField"), QStringLiteral("hunter2") },
            { QStringLiteral("Import"), QString() },
        };
        flow.verdictField = QStringLiteral("advAcctLabelField");
        flow.verdictText = QStringLiteral("issue174");
        return flow;
    }
    return {};
}

QString ShellWebInputDriver::appToDrive() const
{
    ShellModulesBackend* backend = m_host->backend();
    for (const QVariant& tile : backend->launcherApps()) {
        const QString name = tile.toMap().value(QStringLiteral("name")).toString();
        if (backend->isWebContainerApp(name) && !flowFor(name).steps.isEmpty())
            return name;
    }
    return {};
}

bool ShellWebInputDriver::hasWork() const
{
    return !appToDrive().isEmpty();
}

bool ShellWebInputDriver::openApp(const QString& app)
{
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
        WebAppSurface* surface = m_host->webSurface(app);
        if (surface && surface->onScreen()
            && MobileWebContainerBackend::instance()->frontmostModule() == app)
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    emit log(QStringLiteral("WRONG: pressing %1's tile did not put its page on screen")
                 .arg(app));
    return false;
}

bool ShellWebInputDriver::ask(const QString& app, const QString& call,
                              const std::function<bool(const QString&)>& answered,
                              int budgetMs)
{
    // THE DRIVER, THEN THE CALL. The script installs itself once and returns
    // immediately when the page already has it, so every step can send it
    // rather than keeping track of which page has been prepared.
    auto* web = MobileWebContainerBackend::instance();
    if (!web->runJavaScriptIn(app, WebPageInput::driverScript())
        || !web->runJavaScriptIn(app, call)) {
        emit log(QStringLiteral("WRONG: this platform cannot put a script in %1's page")
                     .arg(app));
        return false;
    }
    // Every line the page prints from here on, each offered exactly once: the
    // predicates below record what they read, so a line seen twice would be
    // read twice.
    int next = m_pageLines.size();
    QElapsedTimer waiting;
    waiting.start();
    for (;;) {
        while (next < m_pageLines.size()) {
            if (answered(m_pageLines.at(next++))) return true;
        }
        if (waiting.elapsed() >= budgetMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
}

void ShellWebInputDriver::run()
{
    const QString app = appToDrive();
    if (app.isEmpty()) {
        emit log(QStringLiteral("web input: no `web` app in this build has a typed flow "
                                "this driver knows"));
        return;
    }
    const TypedFlow flow = flowFor(app);
    if (!openApp(app)) return;

    // WHAT THE PAGE OFFERS, BEFORE ANYTHING IS PRESSED. It is the line that
    // separates the two failures below from each other: a step that finds no
    // control is either a module naming its controls differently or an
    // accessibility tree that never woke up, and only this says which.
    if (!ask(app, WebPageInput::describeCall(),
             [](const QString& line) {
                 return line.contains(WebPageInput::marker())
                        && (line.contains(QLatin1String("control(s)"))
                            || line.contains(QLatin1String("tree is empty")));
             },
             kAnswerBudgetMs)) {
        emit log(QStringLiteral("WRONG: %1's page never answered a script -- the page is "
                                "up and nothing in it is listening").arg(app));
        return;
    }

    for (const Step& step : flow.steps) {
        const bool typing = !step.text.isEmpty();
        const QString call = typing ? WebPageInput::typeCall(step.control, step.text)
                                    : WebPageInput::pressCall(step.control);
        // A refusal ENDS the wait as surely as the answer does, and the two are
        // told apart afterwards: waiting a whole budget out for a control the
        // page has already said it does not have proves nothing and costs 15 s.
        bool refused = false;
        const auto done = [&](const QString& line) {
            refused = WebPageInput::refusalReported(line, step.control);
            if (refused) return true;
            return typing ? WebPageInput::valueReported(line, step.control).has_value()
                          : WebPageInput::pressReported(line, step.control);
        };
        if (!ask(app, call, done, kAnswerBudgetMs)) {
            emit log(QStringLiteral("WRONG: %1's page said nothing about '%2' -- see the "
                                    "`logos-drive:` lines above").arg(app, step.control));
            return;
        }
        if (refused) {
            emit log(QStringLiteral("WRONG: %1's page has no reachable control called "
                                    "'%2' -- the names it does have are on the line above")
                         .arg(app, step.control));
            return;
        }
        emit log(typing
                     ? QStringLiteral("web input: typed %1 character(s) into %2's '%3'")
                           .arg(step.text.size()).arg(app, step.control)
                     : QStringLiteral("web input: pressed %1's '%2'").arg(app, step.control));
    }

    // ── the verdict ───────────────────────────────────────────────────────
    //
    // Read back AFTER the whole flow, not as part of the step that typed it: a
    // field that still holds what was typed once the form has been submitted is
    // the module's state, and a value captured inside the typing step could be
    // the driver reading its own echo.
    QString held;
    const bool answered = ask(app, WebPageInput::readCall(flow.verdictField),
        [&](const QString& line) {
            if (const auto value = WebPageInput::valueReported(line, flow.verdictField)) {
                held = *value;
                return true;
            }
            return WebPageInput::refusalReported(line, flow.verdictField);
        },
        kAnswerBudgetMs);

    if (!answered) {
        emit log(QStringLiteral("WRONG: %1's '%2' never reported what it holds")
                     .arg(app, flow.verdictField));
        return;
    }
    if (held != flow.verdictText) {
        emit log(QStringLiteral("WRONG: %1's '%2' was typed '%3' and holds '%4' -- the "
                                "keys did not reach the field")
                     .arg(app, flow.verdictField, flow.verdictText, held));
        return;
    }
    emit log(QStringLiteral("TYPED TEXT REACHES A WEB APP'S PAGE: %1's '%2' holds '%3', "
                            "put there by real key events at the page")
                 .arg(app, flow.verdictField, held));
    // HELD, so a screen recording of the run shows the filled form rather than
    // whatever the next pass navigates to.
    settle(kHoldMs);
}
