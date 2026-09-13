#include "ShellCallDriver.h"

#include "ICoreRuntime.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>
#include <logos_object.h>
#include <logos_types.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QVariant>

using basecamp::appmanager::ModuleCall;
using basecamp::appmanager::ModuleCallScript;

namespace {

// How long a `web` module has to bring its page up before the first call is
// given up on. Generous on purpose: the page is a webview that has to mount,
// fetch its document and instantiate a wasm image, and on a cold phone launch
// that is seconds rather than milliseconds.
constexpr int kReachableMs = 60000;
constexpr int kCallTimeoutMs = 60000;

// HOW LONG THE LOOP TURNS AFTER THE LAST CALL, and it is not padding.
//
// A call RETURNING is not the same as a call being over. A `web` module runs on
// the page's event loop, and the page runs on THIS thread — so while this
// driver is making calls the page makes almost no progress, and the work a call
// set in motion (a module asking another module something, a timer it armed
// when it was admitted) only runs once this stops. A void method is the plain
// case: the frame is queued and the call returns before the module has seen it.
//
// MEASURED, iPad Air 13-inch simulator: the wallet UI's `web` variant asked
// keystore_module for accounts, the reply reassembled on the host — and the
// Modules-tab driver's load/unload round trip destroyed the module in the same
// millisecond, because main.cpp's settle had expired while the page was still
// starved. Nothing had gone wrong; nothing had been given time either.
//
// A run with a `--call` script is a developer's, never a cold-start
// measurement, so this costs nothing that is measured elsewhere.
constexpr int kSettleMs = 8000;

// What a module answered, as one line. A QVariant of anything structured is
// printed as JSON so the answer is readable AND machine-readable -- a driver on
// the other end of a device console is reading this.
QString describe(const QVariant& value)
{
    if (!value.isValid())
        return QStringLiteral("(no value)");
    if (value.typeId() == QMetaType::QString)
        return value.toString();
    if (value.typeId() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    const QJsonDocument doc = QJsonDocument::fromVariant(value);
    if (!doc.isNull())
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    return value.toString();
}

} // namespace

ShellCallDriver::ShellCallDriver(ICoreRuntime* core, const ModuleCallScript& script,
                                 QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_script(script)
{
}

ShellCallDriver::~ShellCallDriver() = default;

bool ShellCallDriver::hasWork() const
{
    return !m_script.isEmpty();
}

bool ShellCallDriver::ensureLoaded(const QString& name)
{
    if (m_core->loadedModules().contains(name))
        return true;
    if (!m_core->knownModules().contains(name)) {
        // Named but not there. Worth one rescan: a module installed earlier in
        // this same launch is on disk and not yet in the core's list.
        m_core->refreshModules();
        if (!m_core->knownModules().contains(name)) {
            emit log(QStringLiteral("call: %1 is not a module this device has").arg(name));
            return false;
        }
    }
    if (m_core->loadModule(name))
        return true;
    emit log(QStringLiteral("call: %1 would not load").arg(name));
    return false;
}

bool ShellCallDriver::awaitReachable(LogosAPIClient* client, const QString& name)
{
    // THE LOAD IS NOT THE PUBLICATION, for a `web` module. The core hands the
    // page to the container and the container answers as soon as the view
    // exists -- but the module's own end of the transport only comes up once
    // that page has fetched its document and instantiated its wasm image, and a
    // call made before then fails outright rather than waiting.
    //
    // `requestObject` is exactly the question "is this target acquirable", and
    // asking it costs no call on the module: a probe that invoked some method
    // of the module's own would be a second contract, and on a keystore it
    // would be one with side effects.
    QElapsedTimer since;
    since.start();
    for (;;) {
        if (LogosObject* obj = client->requestObject(name, Timeout(1000))) {
            obj->release();
            return true;
        }
        if (since.elapsed() > kReachableMs) {
            emit log(QStringLiteral("call: %1 never became reachable").arg(name));
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }
}

void ShellCallDriver::run()
{
    if (!hasWork())
        return;

    // EVERY REFUSAL FIRST, for the reason the catalog driver prints its own: a
    // mistyped flag that silently did nothing reads exactly like a module that
    // did not answer.
    for (const QString& refusal : m_script.refusals())
        emit log(QStringLiteral("call: %1").arg(refusal));

    LogosAPI api(QStringLiteral("shell_call_driver"));

    for (const QString& module : m_script.modules()) {
        if (!ensureLoaded(module))
            continue;
        LogosAPIClient* client = api.getClient(module);
        if (!client) {
            emit log(QStringLiteral("call: no client for %1").arg(module));
            continue;
        }
        if (!awaitReachable(client, module))
            continue;
        emit log(QStringLiteral("call: %1 is loaded and answering").arg(module));
    }

    int made = 0;
    int failed = 0;
    for (const ModuleCall& call : m_script.calls()) {
        LogosAPIClient* client = api.getClient(call.module);
        if (!client) {
            emit log(QStringLiteral("CALL FAILED %1: no client").arg(call.source));
            ++failed;
            continue;
        }

        logos::CallError err;
        const QVariant raw = client->invokeRemoteMethod(call.module, call.method, call.args,
                                                        Timeout(kCallTimeoutMs), &err);
        if (!err.ok()) {
            emit log(QStringLiteral("CALL FAILED %1: %2 (%3)")
                         .arg(call.source, QString::fromStdString(err.message),
                              QString::fromStdString(err.code)));
            ++failed;
            continue;
        }

        // A `result` method comes back as the Qt LogosResult the Native
        // container re-materialises from the module's {success, value, error}
        // JSON; anything else is a plain value and is its own answer. Same fork
        // as NetworkSmokeRunner's, for the same reason.
        if (raw.canConvert<LogosResult>()) {
            const LogosResult lr = raw.value<LogosResult>();
            if (!lr.success) {
                emit log(QStringLiteral("CALL FAILED %1: %2")
                             .arg(call.source, lr.error.toString()));
                ++failed;
                continue;
            }
            emit log(QStringLiteral("CALL OK %1 -> %2").arg(call.source, describe(lr.value)));
        } else {
            emit log(QStringLiteral("CALL OK %1 -> %2").arg(call.source, describe(raw)));
        }
        ++made;
    }

    emit log(QStringLiteral("CALLS: %1 ok, %2 failed").arg(made).arg(failed));

    // See kSettleMs: the calls are made, and what they started has not run yet.
    emit log(QStringLiteral("calls: settling for %1 ms so the modules' own "
                            "asynchronous work can finish").arg(kSettleMs));
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < kSettleMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}
