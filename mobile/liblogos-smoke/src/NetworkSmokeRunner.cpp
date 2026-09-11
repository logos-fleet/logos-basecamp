#include "NetworkSmokeRunner.h"

#include "BundledSetCoreRuntime.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>
#include <logos_types.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QVariantList>
#include <QVariantMap>

namespace {

const char* kLibp2p   = "libp2p_module";
const char* kDelivery = "delivery_module";
const char* kChat     = "chat_module";

// One value from the app's own command line or its environment, in that order.
// argv is how a simulator and a device runner both pass it (simctl launch /
// devicectl process launch take trailing arguments; Android's QtActivity reads
// the `applicationArguments` intent extra), and the environment is the fallback
// a human has.
QString argOrEnv(const QString& flag, const char* envName, const QString& fallback = {})
{
    const QStringList args = QCoreApplication::arguments();
    const int i = args.indexOf(flag);
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    const QByteArray env = qgetenv(envName);
    if (!env.isEmpty())
        return QString::fromUtf8(env);
    return fallback;
}

QString jsonCompact(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Any QVariant a module answered, as one line for the log. QJsonDocument only
// takes a container, and plenty of these answers are a bare string or number,
// so a scalar prints as itself rather than as an empty document.
QString asJson(const QVariant& v)
{
    const QJsonDocument doc = QJsonDocument::fromVariant(v);
    if (doc.isNull())
        return v.toString();
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact)).trimmed();
}

} // namespace

NetworkSmokeRunner::NetworkSmokeRunner(BundledSetCoreRuntime* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
    m_peer = argOrEnv(QStringLiteral("--peer"), "LOGOS_SMOKE_PEER");
    m_topic = argOrEnv(QStringLiteral("--topic"), "LOGOS_SMOKE_TOPIC",
                       QStringLiteral("logos-smoke"));
    m_chatPeer = argOrEnv(QStringLiteral("--chat-peer"), "LOGOS_SMOKE_CHAT_PEER");

    // `/ip4/.../tcp/9500/p2p/16Uiu2...` is ONE string to a human and two
    // arguments to connectPeer, which takes the id and the addresses apart.
    const int p2p = m_peer.indexOf(QStringLiteral("/p2p/"));
    if (p2p > 0) {
        m_peerAddr = m_peer.left(p2p);
        m_peerId = m_peer.mid(p2p + 5);
    }
}

NetworkSmokeRunner::~NetworkSmokeRunner() = default;

bool NetworkSmokeRunner::hasWork() const
{
    const QStringList loaded = m_core->loadedModules();
    return loaded.contains(QLatin1String(kLibp2p)) || loaded.contains(QLatin1String(kChat));
}

bool NetworkSmokeRunner::call(LogosAPIClient* client, const QString& module,
                              const QString& method, const QVariantList& args,
                              QVariant* value, int timeoutMs, bool quiet)
{
    logos::CallError err;
    const QVariant raw = client->invokeRemoteMethod(module, method, args,
                                                    Timeout(timeoutMs), &err);
    if (!err.ok()) {
        if (!quiet)
            emit log(QStringLiteral("  %1.%2 failed: %3 (%4)")
                     .arg(module, method,
                          QString::fromStdString(err.message),
                          QString::fromStdString(err.code)));
        return false;
    }
    // A `result` method comes back as the Qt LogosResult the Native container
    // re-materialises from the module's {success, value, error} JSON
    // (bare_module_glue.cpp). Anything else is a plain value and is its own
    // answer.
    if (raw.canConvert<LogosResult>()) {
        const LogosResult lr = raw.value<LogosResult>();
        if (!lr.success) {
            if (!quiet)
                emit log(QStringLiteral("  %1.%2: %3")
                             .arg(module, method, lr.error.toString()));
            return false;
        }
        if (value) *value = lr.value;
        return true;
    }
    if (value) *value = raw;
    return true;
}

bool NetworkSmokeRunner::run()
{
    bool ok = true;
    const QStringList loaded = m_core->loadedModules();

    if (loaded.contains(QLatin1String(kLibp2p)))
        ok = runLibp2p() && ok;
    else
        emit log(QStringLiteral("libp2p_module: not in this Bundled set"));

    if (loaded.contains(QLatin1String(kDelivery)))
        emit log(QStringLiteral("delivery_module: loaded (the chat core below runs on it)"));

    if (loaded.contains(QLatin1String(kChat)))
        ok = runChat() && ok;
    else
        emit log(QStringLiteral("chat_module: not in this Bundled set"));

    return ok;
}

bool NetworkSmokeRunner::runLibp2p()
{
    LogosAPI api(QStringLiteral("mobile_host"));
    LogosAPIClient* client = api.getClient(QLatin1String(kLibp2p));
    if (!client) {
        emit log(QStringLiteral("no client for libp2p_module"));
        return false;
    }
    const QString mod = QLatin1String(kLibp2p);

    // A fresh node from a call-time config, so the listen address and the
    // transport are this run's choice rather than the module's defaults.
    // Port 0: a phone has no port to reserve and nothing dials IN here.
    QJsonObject cfg;
    cfg["addrs"] = QJsonArray{ QStringLiteral("/ip4/0.0.0.0/tcp/0") };
    cfg["transport"] = QStringLiteral("tcp");
    cfg["mountGossipsub"] = true;
    // No self-echo. The default delivers this node's own publishes back to it,
    // and a message read off the queue would then prove only that the module
    // can talk to itself -- which is exactly the wrong thing to accept as
    // evidence of an exchange with a peer.
    cfg["gossipsubTriggerSelf"] = false;

    QElapsedTimer clock;
    clock.start();
    QVariant ignored;
    if (!call(client, mod, QStringLiteral("createNode"),
              QVariantList{ jsonCompact(cfg) }, &ignored))
        return false;
    if (!call(client, mod, QStringLiteral("start"), QVariantList{}, &ignored))
        return false;
    emit log(QStringLiteral("libp2p node created and started in %1 ms").arg(clock.elapsed()));

    QVariant peerId;
    if (call(client, mod, QStringLiteral("getNodeInfo"),
             QVariantList{ QStringLiteral("PeerId") }, &peerId))
        emit log(QStringLiteral("  PeerId: %1").arg(peerId.toString()));
    QVariant addrs;
    if (call(client, mod, QStringLiteral("getNodeInfo"),
             QVariantList{ QStringLiteral("Multiaddrs") }, &addrs))
        emit log(QStringLiteral("  Multiaddrs: %1").arg(asJson(addrs)));

    if (m_peerId.isEmpty()) {
        emit log(QStringLiteral("no --peer given; dial and gossipsub skipped"));
        emit log(QStringLiteral("LIBP2P NODE OK"));
        return true;
    }
    const bool exchanged = exchangeWithPeer(client);
    emit log(exchanged ? QStringLiteral("LIBP2P NODE + PEER EXCHANGE OK")
                       : QStringLiteral("WRONG: the peer exchange did not complete"));
    return exchanged;
}

bool NetworkSmokeRunner::exchangeWithPeer(LogosAPIClient* client)
{
    const QString mod = QLatin1String(kLibp2p);
    QVariant out;

    // SUBSCRIBE FIRST. Gossipsub only forwards a topic to a peer that is in its
    // mesh for it, and the mesh is built from the subscription both ends
    // announce when the connection comes up. Subscribing after the dial races
    // the desktop's first publish.
    if (!call(client, mod, QStringLiteral("gossipsubSubscribe"),
              QVariantList{ m_topic }, &out))
        return false;
    emit log(QStringLiteral("  subscribed to '%1'").arg(m_topic));

    QElapsedTimer clock;
    clock.start();
    if (!call(client, mod, QStringLiteral("connectPeer"),
              QVariantList{ m_peerId, QVariantList{ m_peerAddr }, qint64(20000) }, &out))
        return false;
    emit log(QStringLiteral("  dialled %1 at %2 in %3 ms")
                 .arg(m_peerId, m_peerAddr).arg(clock.elapsed()));

    if (call(client, mod, QStringLiteral("connectedPeers"), QVariantList{ qint64(0) }, &out))
        emit log(QStringLiteral("  connected peers: %1").arg(asJson(out)));

    // A nonce, so what comes back is provably an answer to THIS run rather than
    // a message left over from the last one.
    const QString nonce = QStringLiteral("phone-%1")
                              .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));

    // Publishing into a mesh that has not formed yet is a message nobody
    // receives, and heartbeat is 1 s. Publish repeatedly and read between
    // attempts, so the first heartbeat after the dial is the slowest this can
    // be rather than the only chance it had.
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (!call(client, mod, QStringLiteral("gossipsubPublish"),
                  QVariantList{ m_topic, nonce }, &out, 30000, /*quiet=*/true)) {
            // Not fatal on its own: a publish before the mesh exists is refused
            // for want of peers, which is what the retries are for.
            if (attempt == 0)
                emit log(QStringLiteral("  publish not accepted yet; retrying while the mesh forms"));
        } else if (attempt == 0) {
            emit log(QStringLiteral("  published '%1' on '%2'").arg(nonce, m_topic));
        }

        // gossipsubNextMessage answers success=false with "timeout waiting for
        // message" when the queue is empty, so an unsuccessful call here is the
        // ordinary empty poll and not a failure.
        QVariant msg;
        if (!call(client, mod, QStringLiteral("gossipsubNextMessage"),
                  QVariantList{ m_topic, qint64(1000) }, &msg, 15000, /*quiet=*/true))
            continue;
        const QString text = msg.toString();
        if (text.isEmpty() || text == nonce)
            continue;
        emit log(QStringLiteral("  gossipsub message from the desktop peer: '%1'").arg(text));
        return true;
    }
    emit log(QStringLiteral("  no gossipsub message arrived from '%1'").arg(m_topic));
    return false;
}

bool NetworkSmokeRunner::runChat()
{
    LogosAPI api(QStringLiteral("mobile_host"));
    LogosAPIClient* client = api.getClient(QLatin1String(kChat));
    if (!client) {
        emit log(QStringLiteral("no client for chat_module"));
        return false;
    }
    const QString mod = QLatin1String(kChat);
    QVariant out;

    // health() first, and it is not ceremony: it is reachable without init and
    // without any lock, so what it answers is whether the Rust core inside the
    // image ANSWERS AT ALL. A false here separates "the module image is broken"
    // from "init failed", and those are different bugs.
    if (!call(client, mod, QStringLiteral("health"), QVariantList{}, &out) || !out.toBool()) {
        emit log(QStringLiteral("chat_module: health() did not answer true"));
        return false;
    }
    emit log(QStringLiteral("chat_module: health() ok (the Rust core answers)"));

    // Storage lives under the instance directory the host assigned; the smoke
    // host assigns one inside the app sandbox (SmokeRunner::prepare).
    //
    // A MAP, not a JSON string. `init(config: ChatConfig)` takes a RECORD, and
    // the Native container marshals each argument as its own JSON value -- so a
    // string argument arrives as a JSON string and the record decoder refuses
    // it. libp2p's `createNode` is the opposite case and takes the string,
    // because its parameter really is a `tstr` the module parses itself.
    QVariantMap chatConfig;
    chatConfig["delivery_preset"] = QStringLiteral("logos.test");
    chatConfig["log_level"] = QStringLiteral("info");
    if (!call(client, mod, QStringLiteral("init"), QVariantList{ chatConfig }, &out, 60000)) {
        emit log(QStringLiteral("chat_module: init failed"));
        return false;
    }
    emit log(QStringLiteral("chat_module: init ok"));

    // Where the chat core is writing its own account of this run, inside the
    // instance directory the host assigned. Printed because it is the only way
    // to see what the Rust core thought when a later call goes wrong, and on a
    // phone nobody can go looking for it without being told the path.
    if (call(client, mod, QStringLiteral("get_log_path"), QVariantList{}, &out))
        emit log(QStringLiteral("  chat log: %1").arg(out.toString()));

    // This installation's own address. Printed because it is the thing a
    // desktop peer needs in order to open a conversation the OTHER way round.
    if (call(client, mod, QStringLiteral("get_address"), QVariantList{}, &out))
        emit log(QStringLiteral("  chat address: %1").arg(out.toString()));

    // A conversation that needs no second party, so "chat_module can create a
    // conversation" is answerable on a phone with nothing else running.
    bool ok = call(client, mod, QStringLiteral("create_group_conversation"),
                   QVariantList{ QStringLiteral("smoke group"), QStringLiteral("") },
                   &out, 60000);
    if (ok)
        emit log(QStringLiteral("  group conversation: %1").arg(asJson(out)));

    // ...and the one the criterion actually names: a 1:1 conversation with the
    // desktop installation, opened from the phone with the address that
    // installation printed.
    if (!m_chatPeer.isEmpty()) {
        const bool direct = call(client, mod, QStringLiteral("create_conversation"),
                                 QVariantList{ m_chatPeer }, &out, 60000);
        emit log(direct
                     ? QStringLiteral("  conversation with the desktop peer: %1").arg(asJson(out))
                     : QStringLiteral("  conversation with the desktop peer FAILED"));
        ok = ok && direct;
    } else {
        emit log(QStringLiteral("no --chat-peer given; the desktop-backed conversation is skipped"));
    }

    if (call(client, mod, QStringLiteral("list_conversations"), QVariantList{}, &out))
        emit log(QStringLiteral("  conversations: %1").arg(asJson(out)));
    if (call(client, mod, QStringLiteral("status"), QVariantList{}, &out))
        emit log(QStringLiteral("  chat status: %1").arg(asJson(out)));

    emit log(ok ? QStringLiteral("CHAT CONVERSATION OK")
                : QStringLiteral("WRONG: chat_module created no conversation"));
    return ok;
}
