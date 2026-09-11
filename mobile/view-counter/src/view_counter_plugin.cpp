#include "view_counter_plugin.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>

#include <QVariant>

namespace {

// The module this view is a front end FOR. Named once: it is the module's
// declared dependency, the name the token is minted against, and the string
// the Bundled-set closure resolves.
const QLatin1String kCounter("bare_counter");

} // namespace

ViewCounterPlugin::ViewCounterPlugin(QObject* parent)
    : ViewCounterSimpleSource(parent)
{
    setCount(0);
    setStatus(QStringLiteral("Ready"));
}

void ViewCounterPlugin::initLogos(LogosAPI* api)
{
    m_logosAPI = api;
    setBackend(this);
    setStatus(QStringLiteral("Connected"));
}

// ── why this module calls another one ───────────────────────────────────────
//
// The count lives in bare_counter, not here. That is the point: a `ui_qml`
// module is a VIEW, and the state it renders belongs to a core module — so
// this is the ordinary shape, not a contrivance to make a closure come out at
// three members.
//
// It is also what makes view_counter's dependencies REAL, which the Bundled
// set then has to satisfy:
//
//   bare_counter       the target of the call;
//   capability_module  how the call is authorised. A module-to-module call
//                      mints its token through capability_module's
//                      requestModule (logos-protocol, LogosAPIClient's
//                      auto-`requestModule` path) — the host's own calls
//                      skip that because the host is the trust root, a
//                      module's do not.
//
// Both are named in metadata.json, so `--bundle view_counter` resolves a
// three-member closure out of the catalog with no list restated anywhere.
QVariant ViewCounterPlugin::callCounter(const QString& method, const QVariantList& args)
{
    if (!m_logosAPI) {
        setStatus(QStringLiteral("no LogosAPI: initLogos has not run"));
        return {};
    }
    LogosAPIClient* client = m_logosAPI->getClient(kCounter);
    if (!client) {
        setStatus(QStringLiteral("no client for %1").arg(kCounter));
        return {};
    }

    logos::CallError err;
    const QVariant result = client->invokeRemoteMethod(kCounter, method, args, Timeout(), &err);
    if (!err.ok()) {
        // The status line IS the diagnostic on a phone: there is no console to
        // read and the view is the only surface. Carry the protocol's own code
        // through rather than collapsing every failure to "error".
        setStatus(QStringLiteral("%1.%2 failed: %3 (%4)")
                      .arg(kCounter, method,
                           QString::fromStdString(err.message),
                           QString::fromStdString(err.code)));
        return {};
    }
    return result;
}

void ViewCounterPlugin::increment()
{
    const QVariant value = callCounter(QStringLiteral("increment"), QVariantList{ QVariant(1) });
    if (!value.isValid())
        return;
    setCount(value.toInt());
    setStatus(QStringLiteral("count = %1").arg(count()));
}

int ViewCounterPlugin::add(int a, int b)
{
    const QVariant result =
        callCounter(QStringLiteral("add"), QVariantList{ QVariant(a), QVariant(b) });
    if (!result.isValid())
        return 0;
    const int value = result.toInt();
    setStatus(QStringLiteral("%1 + %2 = %3").arg(a).arg(b).arg(value));
    return value;
}
