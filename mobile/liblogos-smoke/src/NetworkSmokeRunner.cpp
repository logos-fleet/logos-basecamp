#include "NetworkSmokeRunner.h"

#include "BundledSetCoreRuntime.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_call_error.h>
#include <logos_types.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QVariantList>
#include <QVariantMap>

namespace {

const QLatin1String kLibp2p("libp2p_module");
const QLatin1String kDelivery("delivery_module");
const QLatin1String kChat("chat_module");
const QLatin1String kCapability("capability_module");
const QLatin1String kPeerIdSeparator("/p2p/");
// The group both ends open messages in. Spelled once here and printed on the
// phone's log, so the desktop half can find the conversation by name rather
// than by being told the id out of band.
const QLatin1String kGroupName("slice 22 group");

// One value from the app's own command line or its environment, in that order.
// argv is how a simulator and a device runner both pass it (simctl launch /
// devicectl process launch take trailing arguments; Android's QtActivity reads
// the `extraappparams` intent extra), and the environment is the fallback a
// human has.
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
    m_topic = argOrEnv(QStringLiteral("--topic"), "LOGOS_SMOKE_TOPIC",
                       QStringLiteral("logos-smoke"));
    m_chatPeer = argOrEnv(QStringLiteral("--chat-peer"), "LOGOS_SMOKE_CHAT_PEER");

    // `/ip4/.../tcp/9500/p2p/16Uiu2...` is ONE string to a human and two
    // arguments to connectPeer.
    const QString peer = argOrEnv(QStringLiteral("--peer"), "LOGOS_SMOKE_PEER");
    const int separator = peer.indexOf(kPeerIdSeparator);
    if (separator > 0) {
        m_peerAddr = peer.left(separator);
        m_peerId = peer.mid(separator + kPeerIdSeparator.size());
    }
}

NetworkSmokeRunner::~NetworkSmokeRunner() = default;

bool NetworkSmokeRunner::inSet(const QString& name) const
{
    for (const QVariant& entry : m_core->bundledSet()) {
        if (entry.toMap().value(QStringLiteral("name")).toString() == name)
            return true;
    }
    return false;
}

bool NetworkSmokeRunner::hasWork() const
{
    // The SET, not what is loaded. A Bundled set is registered at start and
    // loaded on demand, and which of the two has happened by the time this is
    // asked is the HOST's business, not the criterion's: the smoke probe
    // brings the whole set up before it gets here, and the Shell leaves
    // loading to the Modules tab -- so asking loadedModules() answered "none
    // in this Bundled set" in a Shell whose set was four networking modules.
    return inSet(kLibp2p) || inSet(kChat);
}

// Loading is idempotent from a caller's point of view but not free, so this
// only asks for what the set has and the core has not got yet.
bool NetworkSmokeRunner::ensureLoaded(const QString& name)
{
    if (!inSet(name))
        return false;
    if (m_core->loadedModules().contains(name))
        return true;
    if (m_core->loadModule(name))
        return true;
    emit log(QStringLiteral("%1: in the Bundled set but would not load").arg(name));
    return false;
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

    // THE BROKER FIRST, and it is not optional wiring: a module-to-module call
    // mints its token through capability_module (LogosAPIClient's
    // auto-requestModule path), and without it loaded the call goes out with
    // no token and the target's ModuleProxy refuses it. Measured on an iPad:
    // chat_module's delivery_module.createNode came back "token not
    // recognized (re-exchange failed)" and the chat core reported
    // delivery_state `error`. The smoke probe never saw this because it loads
    // the whole set before it gets here; the Shell loads on demand, so the
    // broker has to be asked for by the code that needs it.
    if (inSet(kCapability) && !ensureLoaded(kCapability))
        emit log(QStringLiteral("capability_module: in the set but not loaded -- "
                                "module-to-module calls will be refused"));

    if (ensureLoaded(kLibp2p))
        ok = runLibp2p() && ok;
    else
        emit log(QStringLiteral("libp2p_module: not in this Bundled set"));

    // Before chat, and by name: chat_module declares delivery_module as a
    // dependency so the core would pull it in anyway, but a set that carries
    // delivery and not chat still has something to say.
    if (ensureLoaded(kDelivery))
        emit log(QStringLiteral("delivery_module: loaded (the chat core below runs on it)"));

    if (ensureLoaded(kChat))
        ok = runChat() && ok;
    else
        emit log(QStringLiteral("chat_module: not in this Bundled set"));

    return ok;
}

bool NetworkSmokeRunner::runLibp2p()
{
    LogosAPI api(QStringLiteral("mobile_host"));
    LogosAPIClient* client = api.getClient(kLibp2p);
    if (!client) {
        emit log(QStringLiteral("no client for libp2p_module"));
        return false;
    }
    const QString mod = kLibp2p;

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
    if (!call(client, mod, QStringLiteral("createNode"), QVariantList{ jsonCompact(cfg) }))
        return false;
    if (!call(client, mod, QStringLiteral("start"), QVariantList{}))
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
    const QString mod = kLibp2p;
    QVariant out;

    // SUBSCRIBE FIRST. Gossipsub only forwards a topic to a peer that is in its
    // mesh for it, and the mesh is built from the subscription both ends
    // announce when the connection comes up. Subscribing after the dial races
    // the desktop's first publish.
    if (!call(client, mod, QStringLiteral("gossipsubSubscribe"), QVariantList{ m_topic }))
        return false;
    emit log(QStringLiteral("  subscribed to '%1'").arg(m_topic));

    QElapsedTimer clock;
    clock.start();
    if (!call(client, mod, QStringLiteral("connectPeer"),
              QVariantList{ m_peerId, QVariantList{ m_peerAddr }, qint64(20000) }))
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
                  QVariantList{ m_topic, nonce }, nullptr, 30000, /*quiet=*/true)) {
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
    LogosAPIClient* client = api.getClient(kChat);
    if (!client) {
        emit log(QStringLiteral("no client for chat_module"));
        return false;
    }
    const QString mod = kChat;
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

    // DELIVERY FIRST, and this is a wait rather than a read. `init` returns as
    // soon as the state is installed; joining the delivery network happens on
    // callbacks afterwards (chat's start_delivery_bootstrap -> createNode ->
    // start), so everything below would otherwise race a node that is still
    // dialling. A conversation opened against a peer while the node is
    // `initialising` is created LOCALLY and its invite goes nowhere -- the
    // phone reports a convo_id and the desktop never hears of it, which is the
    // one failure this criterion cannot tell from success by looking at the
    // phone.
    if (awaitDeliveryOnline(client, 120000) != QLatin1String("online")) {
        emit log(QStringLiteral("chat_module: the delivery node is not online"));
        return false;
    }

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

    // EVERYTHING ABOVE IS WHAT "CHAT USABLE" MEANS on this device: the core
    // answers, its state is installed, and its delivery node has joined the
    // network, which is the last of the three a user waits for. A host times
    // its cold start off this signal.
    emit chatUsable();

    // The criterion: a GROUP the desktop installation is a member of, with a
    // message crossing in each direction. Without a peer to invite, the group
    // is still created -- "chat_module can open a conversation" is answerable
    // on a phone with nothing else running -- and the exchange is skipped by
    // name rather than failed.
    bool ok = false;
    if (!m_chatPeer.isEmpty()) {
        ok = exchangeInGroup(client);
    } else {
        ok = call(client, mod, QStringLiteral("create_group_conversation"),
                  QVariantList{ kGroupName, QStringLiteral("") }, &out, 60000);
        if (ok)
            emit log(QStringLiteral("  group conversation: %1").arg(asJson(out)));
        emit log(QStringLiteral("no --chat-peer given; the two-party group exchange is skipped"));
    }

    if (call(client, mod, QStringLiteral("list_conversations"), QVariantList{}, &out))
        emit log(QStringLiteral("  conversations: %1").arg(asJson(out)));
    if (call(client, mod, QStringLiteral("status"), QVariantList{}, &out))
        emit log(QStringLiteral("  chat status: %1").arg(asJson(out)));

    emit log(ok ? QStringLiteral("CHAT CONVERSATION OK")
                : QStringLiteral("WRONG: chat_module did not see its conversation through"));
    return ok;
}

// The slice-22 criterion, and it is deliberately ONE group rather than the
// 1:1 conversation slice 21 opened: a group is the shape the criterion names,
// and it is also the harder of the two -- a member has to be invited, the
// group has to COMMIT that invite, and only then can either side read what the
// other writes. A 1:1 invite needs no commit and would pass while the group
// path was broken.
//
// The group is created HERE rather than on the desktop because
// create_group_conversation makes the caller its only member and
// add_group_member is how it grows: the phone is the side under test, so the
// phone is the side that owns the conversation.
bool NetworkSmokeRunner::exchangeInGroup(LogosAPIClient* client)
{
    const QString mod = kChat;
    QVariant out;
    QElapsedTimer clock;
    clock.start();

    if (!call(client, mod, QStringLiteral("create_group_conversation"),
              QVariantList{ kGroupName, QStringLiteral("slice 22") }, &out, 60000)) {
        emit log(QStringLiteral("  the two-party group could not be created"));
        return false;
    }
    const QString convo = out.toString();
    if (convo.isEmpty()) {
        emit log(QStringLiteral("  create_group_conversation answered no convo_id"));
        return false;
    }
    emit log(QStringLiteral("  two-party group: %1").arg(convo));

    if (!call(client, mod, QStringLiteral("add_group_member"),
              QVariantList{ convo, m_chatPeer }, &out, 60000)) {
        emit log(QStringLiteral("  the desktop peer could not be invited"));
        return false;
    }
    emit log(QStringLiteral("  invited the desktop peer: %1").arg(m_chatPeer));

    // WAIT FOR THE COMMIT, and this is the step that cannot be skipped. The
    // invite is delivered and committed asynchronously; a message sent while
    // the member is still `pending` is encrypted to an epoch that member is
    // not in, so the desktop never sees it -- and the phone reports a
    // successful send either way.
    // Four minutes, and it is not padding: two logoscore installations on one
    // laptop, on the same delivery preset, took 58 seconds to commit an invite
    // -- the commit is a round trip through the delivery network, not a local
    // operation, and a phone on wifi is the slower end of one.
    if (!awaitGroupCommit(client, convo, 240000)) {
        emit log(QStringLiteral("  the group never committed the desktop peer"));
        return false;
    }

    // OUT. The nonce is what the desktop end matches on, so that "the phone
    // sent a message" is confirmed from the far side rather than from the
    // phone's account of its own call.
    const QString mine = QStringLiteral("phone-%1")
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    if (!call(client, mod, QStringLiteral("send_message"),
              QVariantList{ convo, mine }, &out, 60000)) {
        emit log(QStringLiteral("  send_message failed"));
        return false;
    }
    emit log(QStringLiteral("  sent '%1' to the group").arg(mine));

    // IN. get_messages rather than the message_received event: this host does
    // not subscribe to the module's IPC event channel, and the stored
    // conversation is the same thing the event announces.
    const QString theirs = awaitInboundMessage(client, convo, 180000);
    if (theirs.isEmpty()) {
        emit log(QStringLiteral("  no message arrived from the desktop peer"));
        return false;
    }
    emit log(QStringLiteral("  message from the desktop peer: '%1'").arg(theirs));
    emit log(QStringLiteral("CHAT GROUP MESSAGE ROUND TRIP OK (%1 ms)").arg(clock.elapsed()));
    return true;
}

// BY COUNT, not by matching the invited address. A GroupMember's `address` is
// the member's DIRECTORY-VERIFIED account address and the contract says it is
// empty when the member claims no confirmed account -- so a roster that has
// committed the desktop peer can report it under a name this side never saw,
// and waiting for the address to appear would time out on a group that is
// working. Two committed members and none pending is the same fact without
// that assumption: this installation is one, and the only invite sent is the
// other.
bool NetworkSmokeRunner::awaitGroupCommit(LogosAPIClient* client, const QString& convo,
                                          int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    bool announced = false;
    for (;;) {
        QVariant out;
        if (call(client, kChat, QStringLiteral("list_group_members"),
                 QVariantList{ convo }, &out, 15000, /*quiet=*/true)) {
            int committed = 0;
            int pending = 0;
            for (const QVariant& entry : out.toList()) {
                if (entry.toMap().value(QStringLiteral("pending")).toBool())
                    ++pending;
                else
                    ++committed;
            }
            if (committed >= 2 && pending == 0) {
                emit log(QStringLiteral("  the group committed the desktop peer in %1 ms "
                                        "(roster: %2)")
                             .arg(clock.elapsed()).arg(asJson(out)));
                return true;
            }
        }
        if (clock.elapsed() >= timeoutMs) {
            emit log(QStringLiteral("  roster after %1 ms: %2").arg(clock.elapsed()).arg(asJson(out)));
            return false;
        }
        if (!announced) {
            emit log(QStringLiteral("  waiting for the group to commit the invite..."));
            announced = true;
        }
        idle(500);
    }
}

QString NetworkSmokeRunner::awaitInboundMessage(LogosAPIClient* client, const QString& convo,
                                                int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    bool announced = false;
    for (;;) {
        QVariant out;
        if (call(client, kChat, QStringLiteral("get_messages"),
                 QVariantList{ convo }, &out, 15000, /*quiet=*/true)) {
            for (const QVariant& entry : out.toList()) {
                const QVariantMap message = entry.toMap();
                // from_self is the module's own answer to "did I write this",
                // and it is the only reliable one: this installation's sends
                // are in the same list, and matching on content would accept
                // an echo of the phone's own message as an exchange.
                if (message.value(QStringLiteral("from_self")).toBool())
                    continue;
                return message.value(QStringLiteral("content")).toString();
            }
        }
        if (clock.elapsed() >= timeoutMs)
            return { };
        if (!announced) {
            emit log(QStringLiteral("  waiting for the desktop peer to write into the group..."));
            announced = true;
        }
        idle(500);
    }
}

// status() is the only window onto the bootstrap, and it is a poll because the
// module's `delivery_state_changed` event is an IPC event channel this host does
// not subscribe to. 500 ms between reads: the transition is a network join, so
// a tighter loop would only add calls.
QString NetworkSmokeRunner::awaitDeliveryOnline(LogosAPIClient* client, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    QString state;
    bool announced = false;
    for (;;) {
        QVariant out;
        if (call(client, kChat, QStringLiteral("status"), QVariantList{}, &out,
                 15000, /*quiet=*/true)) {
            const QVariantMap status = out.toMap();
            state = status.value(QStringLiteral("delivery_state")).toString();
            if (state == QLatin1String("online")) {
                emit log(QStringLiteral("  delivery node online in %1 ms").arg(clock.elapsed()));
                return state;
            }
            // An `error` is terminal -- the bootstrap reported a failed
            // createNode or start and nothing retries it -- so waiting out the
            // bound would only delay the same answer.
            if (state == QLatin1String("error")) {
                emit log(QStringLiteral("  delivery node failed: %1")
                             .arg(status.value(QStringLiteral("detail")).toString()));
                return state;
            }
        }
        if (clock.elapsed() >= timeoutMs)
            return state;
        if (!announced) {
            emit log(QStringLiteral("  waiting for the delivery node to come online..."));
            announced = true;
        }
        idle(500);
    }
}

void NetworkSmokeRunner::idle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
