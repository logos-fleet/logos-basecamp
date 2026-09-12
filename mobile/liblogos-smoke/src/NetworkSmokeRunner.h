#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class BundledSetCoreRuntime;
class LogosAPIClient;

// The networking half of the smoke: what the Bundled set's REAL modules do once
// they are loaded, as opposed to that they load at all.
//
// Nothing here is required for a Bundled set to be valid — a set that carries
// only the counter has no node to create — so every step is conditional on the
// module being in the set, and a set without it reports "not in this set" and
// passes. The names ARE spelled out, unlike everywhere else in this host, and
// that is the difference between the two jobs: the container's job is to load
// whatever `--bundle` named, and this one's is to exercise three modules whose
// APIs it has to know.
//
// WHERE THE PEER COMES FROM. A phone cannot discover a laptop, so the desktop
// peer is handed in: `--peer <multiaddr>/p2p/<peerId>` on the app's own command
// line, or LOGOS_SMOKE_PEER in its environment. Without one the node is still
// created, started and reported — that half needs no second party — and the
// dial and the gossipsub exchange are skipped by name.
class NetworkSmokeRunner : public QObject
{
    Q_OBJECT
public:
    explicit NetworkSmokeRunner(BundledSetCoreRuntime* core, QObject* parent = nullptr);
    ~NetworkSmokeRunner() override;

    // Whether this set carries anything for run() to exercise. False when it
    // holds none of the networking modules.
    bool hasWork() const;

    // Runs what hasWork() advertised. False if a step that was attempted failed;
    // a step that was skipped by name is not a failure.
    bool run();

signals:
    void log(const QString& line);

    // The chat core is up: initialised, its delivery node online, and the
    // first conversation openable. A host times its own "cold start to Chat
    // usable" off this -- the clock's origin is main()'s, which is the
    // caller's to know, so what is emitted is the fact and not a number.
    void chatUsable();

private:
    // Whether the app SHIPS `name`, which is a different question from
    // whether it is running.
    bool inSet(const QString& name) const;
    // `name` is in the set and the core has it loaded, loading it if it did
    // not. False when the set does not carry it, or it would not load.
    bool ensureLoaded(const QString& name);
    // One libp2p node, created and started in the module inside this process.
    bool runLibp2p();
    // The dial + gossipsub exchange against the desktop peer. Only reached when
    // a peer was supplied.
    bool exchangeWithPeer(LogosAPIClient* client);
    // chat_module over the delivery_module beneath it.
    bool runChat();
    // The criterion this host exists to answer: a GROUP conversation with the
    // desktop installation in it, one message out of this device and one
    // message in from that peer. Only reached when a --chat-peer was supplied.
    bool exchangeInGroup(LogosAPIClient* client);
    // Block until `convo`'s roster holds two committed members and no pending
    // invite, or the bound expires.
    bool awaitGroupCommit(LogosAPIClient* client, const QString& convo, int timeoutMs);
    // Block until a message this installation did NOT send appears in `convo`.
    // Answers its content, or an empty string when the bound expires.
    QString awaitInboundMessage(LogosAPIClient* client, const QString& convo,
                                int timeoutMs);
    // Block until the chat core reports its delivery node online, or the bound
    // expires. Answers the state it last saw.
    QString awaitDeliveryOnline(LogosAPIClient* client, int timeoutMs);

    // Turn the host's event loop for `ms` without returning to it. A nested
    // loop rather than processEvents(): the IPC replies these polls are
    // waiting for arrive on the host's loop, and processEvents returns the
    // instant the queue is empty -- which turns an interval into a busy poll.
    void idle(int ms);

    // A `result`-returning universal method: unwrap the LogosResult the Native
    // container re-materialises from the module's JSON, log its error if it
    // failed, and hand back the value. A null `value` is for the calls whose
    // answer nothing reads.
    //
    // `quiet` is for the calls whose FAILURE is an ordinary outcome -- an empty
    // gossipsub queue answers "timeout waiting for message", and polling it
    // thirty times would otherwise bury the run's real lines under thirty
    // identical ones.
    bool call(LogosAPIClient* client, const QString& module, const QString& method,
              const QVariantList& args, QVariant* value = nullptr,
              int timeoutMs = 30000, bool quiet = false);

    BundledSetCoreRuntime* m_core;
    // The desktop peer's multiaddr and id: `--peer` hands them over as one
    // string and connectPeer takes them apart, so they are split once here.
    QString m_peerId;
    QString m_peerAddr;
    QString m_topic;
    QString m_chatPeer;    // the desktop installation's chat address
};
