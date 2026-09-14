#include "ShellConsentDriver.h"

#include "ShellModulesBackend.h"
#include "appmanager/StoreAppManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

using basecamp::appmanager::ConsentScript;

namespace {

// How long a step waits. Generous, and for two different reasons: the PROMPT
// waits on a `web` module bringing a page and a wasm image up on a cold phone
// launch before it makes its first call, and a RETRY waits on the fixture's own
// retry interval plus a real round trip through the container and the broker.
constexpr int kPromptWaitMs = 90000;
constexpr int kRetryWaitMs  = 40000;

// How long "no prompt appeared" is worth asserting for. A negative has to be
// bounded by something, and this is the window the positive case reliably lands
// inside -- a prompt that came later than every prompt ever measured would be a
// different defect and this would catch it as a pass.
constexpr int kNoPromptWindowMs = 15000;

// What the page prints when a call it made comes back. The fixture's line, and
// the only window into a canvas there is: `logos-view: callModuleAsync -> <payload>`.
const QRegularExpression& callResultPattern()
{
    static const QRegularExpression re(QStringLiteral("logos-view: callModuleAsync -> (.*)$"));
    return re;
}

// WHAT ONE OF THOSE ANSWERS WAS. A FAILURE IS AN OBJECT WITH `error` IN IT; a
// success is the bare return value (logos-module-builder's
// logos_view_wasm_host.cpp). Same rule the fixture's own QML applies, so the two
// cannot disagree about what a refusal is.
struct CallAnswer {
    bool    refused = false;
    QString code;
    // The refuser's own sentence, or the raw payload when it sent none: a
    // console line that named only the code would drop the half a person reads.
    QString message;

    QString describe() const { return QStringLiteral("%1: %2").arg(code, message); }
};

CallAnswer readAnswer(const QString& payload)
{
    CallAnswer answer;
    const QJsonDocument doc = QJsonDocument::fromJson(payload.trimmed().toUtf8());
    if (!doc.isObject() || !doc.object().contains(QStringLiteral("error")))
        return answer;

    const QJsonObject obj = doc.object();
    answer.refused = true;
    answer.code = obj.value(QStringLiteral("error")).toString();
    answer.message = obj.value(QStringLiteral("message")).toString();
    if (answer.message.isEmpty())
        answer.message = payload;
    return answer;
}

// Whether the dialog on screen is the one this plan is about. Asked by the wait
// and again by `grant`, which answers a prompt only when there is one.
bool promptIsFor(const QVariantMap& prompt, const ConsentScript::Plan& plan)
{
    return prompt.value(QStringLiteral("caller")).toString() == plan.caller
        && prompt.value(QStringLiteral("target")).toString() == plan.target;
}

// The pair, as every console line in here names it.
QString pairName(const ConsentScript::Plan& plan)
{
    return QStringLiteral("%1 -> %2").arg(plan.caller, plan.target);
}

} // namespace

ShellConsentDriver::ShellConsentDriver(ShellModulesBackend* backend,
                                       const ConsentScript& script, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_script(script)
{
    // EVERY LINE, FROM BEFORE THE FIRST STEP RUNS. The page makes its first call
    // ~400 ms after its QML arrives, which on a device is well before this
    // driver's turn -- so a driver that started listening when it started
    // working would miss the refusal it exists to assert.
    connect(m_backend, &ShellModulesBackend::log, this,
            [this](const QString& line) { m_lines << line; });
}

bool ShellConsentDriver::hasWork() const
{
    return !m_script.isEmpty();
}

bool ShellConsentDriver::pumpUntil(int ms, const std::function<bool()>& done)
{
    QElapsedTimer since;
    since.start();
    while (!done()) {
        if (since.elapsed() >= ms)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

QVariantMap ShellConsentDriver::awaitPrompt(const Plan& plan, int ms)
{
    basecamp::appmanager::StoreAppManager* manager = m_backend->appManager();
    QVariantMap found;
    pumpUntil(ms, [&]() {
        const QVariantMap p = manager->consentPrompt();
        if (!promptIsFor(p, plan))
            return false;
        found = p;
        return true;
    });
    return found;
}

QString ShellConsentDriver::awaitCallResult(const Plan& plan, int fromLine, int ms)
{
    QString payload;
    pumpUntil(ms, [&]() {
        for (int i = fromLine; i < m_lines.size(); ++i) {
            // The caller's OWN console. Two Downloaded modules can be retrying
            // at once, and a line from the wrong one would answer the wrong
            // question.
            if (!m_lines.at(i).startsWith(plan.caller))
                continue;
            const QRegularExpressionMatch m = callResultPattern().match(m_lines.at(i));
            if (m.hasMatch()) {
                payload = m.captured(1).trimmed();
                return true;
            }
        }
        return false;
    });
    return payload;
}

QString ShellConsentDriver::stateOf(const Plan& plan) const
{
    return m_backend->consentStatus(plan.caller, plan.target)
        .value(QStringLiteral("state")).toString();
}

QString ShellConsentDriver::describeStatus(const Plan& plan) const
{
    const QVariantMap s = m_backend->consentStatus(plan.caller, plan.target);
    if (s.isEmpty())
        return QStringLiteral("capability_module did not answer");
    return QStringLiteral("state=%1 callerOrigin=%2 targetOrigin=%3 -- %4")
        .arg(s.value(QStringLiteral("state")).toString(),
             s.value(QStringLiteral("callerOrigin")).toString(),
             s.value(QStringLiteral("targetOrigin")).toString(),
             s.value(QStringLiteral("reason")).toString());
}

bool ShellConsentDriver::awaitPromptOverAFailingCall(const Plan& plan)
{
    basecamp::appmanager::StoreAppManager* manager = m_backend->appManager();
    const QString pair = pairName(plan);

    const QVariantMap prompt = awaitPrompt(plan, kPromptWaitMs);
    if (prompt.isEmpty()) {
        emit log(QStringLiteral("CONSENT FAILED: no prompt for %1 in %2 ms; %3")
                     .arg(pair).arg(kPromptWaitMs).arg(describeStatus(plan)));
        return false;
    }
    // WHAT A USER WOULD HAVE READ, before anything is answered -- the same
    // reason the signer prompt is logged before it is approved.
    emit log(QStringLiteral("consent: PROMPT for %1 -- \"%2\" (%3 pending)")
                 .arg(pair, prompt.value(QStringLiteral("question")).toString())
                 .arg(manager->pendingConsentCount()));
    emit log(QStringLiteral("consent: capability_module says %1").arg(describeStatus(plan)));

    // THE CALL THAT IS FAILING MEANWHILE. A prompt with a call that died at
    // MODULE_NOT_LOADED behind it proves nothing: that call never reached the
    // gate.
    const QString pending = awaitCallResult(plan, 0, kRetryWaitMs);
    if (pending.isEmpty()) {
        emit log(QStringLiteral("CONSENT FAILED: %1 never reported a call result")
                     .arg(plan.caller));
        return false;
    }
    const CallAnswer undecided = readAnswer(pending);
    if (!undecided.refused || undecided.code == QLatin1String("MODULE_NOT_LOADED")) {
        emit log(QStringLiteral("CONSENT FAILED: while undecided, %1's call came back "
                                "'%2' -- which is not the gate refusing it")
                     .arg(pair, pending));
        return false;
    }
    emit log(QStringLiteral("consent: while undecided the call failed -- %1")
                 .arg(undecided.describe()));
    return true;
}

bool ShellConsentDriver::runDismiss(const Plan& plan)
{
    if (!awaitPromptOverAFailingCall(plan))
        return false;

    m_backend->appManager()->dismissConsent();
    const QString state = stateOf(plan);
    emit log(QStringLiteral("consent: dismissed (not now); capability_module still "
                            "says state=%1").arg(state));
    // A dismissal must record NOTHING: turning "not now" into "never" is the one
    // answer the user did not give.
    if (state == QLatin1String("granted") || state == QLatin1String("denied")) {
        emit log(QStringLiteral("CONSENT FAILED: a dismissal was recorded as '%1'")
                     .arg(state));
        return false;
    }
    return true;
}

bool ShellConsentDriver::runDeny(const Plan& plan)
{
    if (!awaitPromptOverAFailingCall(plan))
        return false;

    const QString pair = pairName(plan);
    const int before = int(m_lines.size());
    m_backend->appManager()->answerConsent(false);
    emit log(QStringLiteral("consent: answered DENY for %1").arg(pair));

    const QString state = stateOf(plan);
    if (state != QLatin1String("denied")) {
        emit log(QStringLiteral("CONSENT FAILED: after the denial capability_module says "
                                "state=%1").arg(state));
        return false;
    }
    // THE CALLER'S NEXT ATTEMPT, which is where a decision actually lands:
    // capability_module cannot hold a dispatch thread open across a dialog, so
    // the refusal is carried by the retry and by nothing else.
    const QString after = awaitCallResult(plan, before, kRetryWaitMs);
    if (after.isEmpty()) {
        emit log(QStringLiteral("CONSENT FAILED: %1 did not call again after the denial")
                     .arg(plan.caller));
        return false;
    }
    const CallAnswer answer = readAnswer(after);
    if (!answer.refused) {
        emit log(QStringLiteral("CONSENT FAILED: the call SUCCEEDED after a denial -- %1")
                     .arg(after));
        return false;
    }
    emit log(QStringLiteral("consent: after the denial the call still fails -- %1")
                 .arg(answer.describe()));
    emit log(QStringLiteral("consent: capability_module says %1").arg(describeStatus(plan)));
    return true;
}

bool ShellConsentDriver::runGrant(const Plan& plan)
{
    basecamp::appmanager::StoreAppManager* manager = m_backend->appManager();
    const QString pair = pairName(plan);

    // NO PROMPT IS WAITED FOR. capability_module does not re-announce a pair it
    // already has a decision for, so a user changing their mind after a denial
    // is a decision and not a second dialog. When one IS on screen (the plain
    // grant-first case) it is answered through the queue, so the queue pops it
    // rather than being left holding a question that has been answered behind
    // its back.
    const int before = int(m_lines.size());
    const bool onScreen = promptIsFor(manager->consentPrompt(), plan);
    if (onScreen)
        manager->answerConsent(true);
    else if (!m_backend->decideConsent(plan.caller, plan.target, true)) {
        emit log(QStringLiteral("CONSENT FAILED: the grant for %1 was not recorded")
                     .arg(pair));
        return false;
    }
    emit log(QStringLiteral("consent: answered GRANT for %1%2")
                 .arg(pair, onScreen ? QStringLiteral(" (from the prompt)")
                                     : QStringLiteral(" (no prompt on screen)")));

    const QString state = stateOf(plan);
    if (state != QLatin1String("granted")) {
        emit log(QStringLiteral("CONSENT FAILED: after the grant capability_module says "
                                "state=%1").arg(state));
        return false;
    }
    const QString after = awaitCallResult(plan, before, kRetryWaitMs);
    if (after.isEmpty()) {
        emit log(QStringLiteral("CONSENT FAILED: %1 did not call again after the grant")
                     .arg(plan.caller));
        return false;
    }
    const CallAnswer answer = readAnswer(after);
    if (answer.refused) {
        emit log(QStringLiteral("CONSENT FAILED: the call is still refused after a grant "
                                "-- %1").arg(answer.describe()));
        return false;
    }
    emit log(QStringLiteral("consent: after the grant the call SUCCEEDS -- %1").arg(after));
    return true;
}

bool ShellConsentDriver::runExpectGranted(const Plan& plan)
{
    basecamp::appmanager::StoreAppManager* manager = m_backend->appManager();
    const QString pair = pairName(plan);

    // THE SECOND LAUNCH. Nothing is answered here: what is asserted is that the
    // answer given by a PREVIOUS run of this app was read back off disk before
    // anything asked, that no prompt comes up, and that the call goes straight
    // through.
    const QString state = stateOf(plan);
    emit log(QStringLiteral("consent: at startup capability_module says %1")
                 .arg(describeStatus(plan)));
    if (state != QLatin1String("granted")) {
        emit log(QStringLiteral("CONSENT FAILED: the grant for %1 did not survive the "
                                "restart (state=%2)").arg(pair, state));
        return false;
    }

    const QString result = awaitCallResult(plan, 0, kRetryWaitMs);
    if (result.isEmpty()) {
        emit log(QStringLiteral("CONSENT FAILED: %1 made no call to assert")
                     .arg(plan.caller));
        return false;
    }
    const CallAnswer answer = readAnswer(result);
    if (answer.refused) {
        emit log(QStringLiteral("CONSENT FAILED: the call was refused on a granted pair "
                                "-- %1").arg(answer.describe()));
        return false;
    }
    emit log(QStringLiteral("consent: the call succeeded with no prompt -- %1").arg(result));

    // AND NO SECOND QUESTION. A grant that persisted but still prompted would
    // satisfy every other check here and be exactly the thing the criterion
    // forbids.
    const bool prompted = pumpUntil(kNoPromptWindowMs, [manager]() {
        return !manager->consentPrompt().isEmpty();
    });
    if (prompted) {
        emit log(QStringLiteral("CONSENT FAILED: a granted pair prompted again -- \"%1\"")
                     .arg(manager->consentPrompt().value(QStringLiteral("question")).toString()));
        return false;
    }
    emit log(QStringLiteral("consent: no prompt in %1 ms, as a remembered grant requires")
                 .arg(kNoPromptWindowMs));
    return true;
}

bool ShellConsentDriver::runStep(const Plan& plan, Step step)
{
    switch (step) {
    case Step::Deny:          return runDeny(plan);
    case Step::Dismiss:       return runDismiss(plan);
    case Step::Grant:         return runGrant(plan);
    case Step::ExpectGranted: return runExpectGranted(plan);
    }
    return false;
}

void ShellConsentDriver::run()
{
    if (!hasWork())
        return;

    // EVERY REFUSAL FIRST, for the reason ShellCatalogDriver prints its own
    // first: a mistyped flag that silently did nothing is indistinguishable
    // from a gate that never fired.
    for (const QString& refusal : m_script.refusals())
        emit log(QStringLiteral("consent: %1").arg(refusal));

    for (const Plan& plan : m_script.plans()) {
        emit log(QStringLiteral("consent: plan %1").arg(plan.source));
        // THE CALLER HAS TO BE RUNNING, and on a relaunch nothing has brought it
        // up: the install that put it on the device was a previous process, and
        // a Store shell's cold start does not load what it discovers. Without
        // this the second launch would assert "no prompt and the call succeeded"
        // about a module that never made a call.
        if (!m_backend->ensureRunning(plan.caller)) {
            emit log(QStringLiteral("CONSENT FAILED: this device does not have '%1'")
                         .arg(plan.caller));
            emit log(QStringLiteral("CONSENT FAILED: %1").arg(plan.source));
            continue;
        }
        bool ok = true;
        for (Step step : plan.steps) {
            emit log(QStringLiteral("consent: step '%1' for %2 -> %3")
                         .arg(ConsentScript::stepName(step), plan.caller, plan.target));
            if (!runStep(plan, step)) {
                ok = false;
                break;
            }
        }
        emit log(ok ? QStringLiteral("CONSENT OK: %1").arg(plan.source)
                    : QStringLiteral("CONSENT FAILED: %1").arg(plan.source));
    }
}
