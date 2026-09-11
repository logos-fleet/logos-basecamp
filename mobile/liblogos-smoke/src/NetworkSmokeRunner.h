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

    // Every step this run is going to attempt, decided from the set and the
    // arguments. Empty when the set carries none of the networking modules.
    bool hasWork() const;

    // Runs what hasWork() advertised. False if a step that was attempted failed;
    // a step that was skipped by name is not a failure.
    bool run();

signals:
    void log(const QString& line);

private:
    // One libp2p node, created and started in the module inside this process.
    bool runLibp2p();
    // The dial + gossipsub exchange against the desktop peer. Only reached when
    // a peer was supplied.
    bool exchangeWithPeer(LogosAPIClient* client);
    // chat_module over the delivery_module beneath it.
    bool runChat();

    // A `result`-returning universal method: unwrap the LogosResult the Native
    // container re-materialises from the module's JSON, log its error if it
    // failed, and hand back the value.
    //
    // `quiet` is for the calls whose FAILURE is an ordinary outcome -- an empty
    // gossipsub queue answers "timeout waiting for message", and polling it
    // thirty times would otherwise bury the run's real lines under thirty
    // identical ones.
    bool call(LogosAPIClient* client, const QString& module, const QString& method,
              const QVariantList& args, QVariant* value, int timeoutMs = 30000,
              bool quiet = false);

    BundledSetCoreRuntime* m_core;
    QString m_peer;        // the whole multiaddr, /p2p/<id> included
    QString m_peerId;      // ...and its two halves, split once
    QString m_peerAddr;
    QString m_topic;
    QString m_chatPeer;    // the desktop installation's chat address
};
