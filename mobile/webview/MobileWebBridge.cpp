#include "webview/MobileWebBridge.h"

#include "web/LogosWebPaths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QRandomGenerator>

#include <utility>

namespace basecamp::web {

namespace {

// The shim, injected at document creation.
//
// It runs BEFORE the page's own scripts, which is what lets a variant's entry
// document read `window.logosQmlRuntimeBase` at the top of its first module
// script and `await window.logosChannelReady` in its loader. The five methods
// and their queueing rules are the desktop container's, deliberately: a module
// page written against the browser SDK must behave identically in all three
// containers, and the way to guarantee that is for all three to hand it the
// same channel.
//
// The POLL LOOP is what is different here, and it is the whole of the
// difference. There is no host object to connect a signal to, so the page asks
// — one fetch outstanding at a time, re-armed the moment the previous settles.
// An empty batch is the host saying "nothing yet", not an error, so the loop
// does not back off on one; it backs off only on a FAILED fetch, which is what
// a torn-down webview looks like from in here.
const char* kShim = R"JS(
(function () {
  window.logosQmlRuntimeBase = '@RUNTIME_BASE@';
  var CONTROL = '@CONTROL_BASE@';

  var pending = [];
  var receiver = null;
  var open = true;

  function deliver(text) {
    if (receiver) {
      // Never inline: the SDK's peer writes with its own registry lock held,
      // and a channel that called back synchronously would re-enter it.
      var cb = receiver;
      setTimeout(function () { cb(text); }, 0);
    } else {
      pending.push(text);
    }
  }

  function pump() {
    if (!open) return;
    fetch(CONTROL + '/poll', { method: 'GET', cache: 'no-store' })
      .then(function (r) { return r.json(); })
      .then(function (batch) {
        if (batch && batch.closed) { open = false; return; }
        var frames = (batch && batch.frames) || [];
        for (var i = 0; i < frames.length; i++) deliver(frames[i]);
        pump();
      })
      .catch(function () {
        // The webview is going away, or the host stopped answering. Re-arm
        // slowly rather than spinning a failing fetch on the UI thread.
        if (open) setTimeout(pump, 250);
      });
  }

  var channel = {
    send: function (text) {
      if (!open) return false;
      fetch(CONTROL + '/send', { method: 'POST', body: String(text) })
        .catch(function () { /* the page is being torn down */ });
      return true;
    },
    setReceiver: function (fn) {
      receiver = fn || null;
      if (!receiver) return;
      var queued = pending;
      pending = [];
      queued.forEach(deliver);
    },
    close: function () {
      if (!open) return;
      open = false;
      // TELL THE HOST. A channel the page has closed is a module that has
      // stopped serving, and the host cannot see that from its own end.
      fetch(CONTROL + '/close', { method: 'POST' }).catch(function () {});
    },
    isOpen: function () { return open; }
  };

  // RESOLVED SYNCHRONOUSLY, unlike the desktop's: there is no handshake to
  // wait for here, because the host's end of every control path exists before
  // the page is asked to load at all.
  window.logosChannelReady = Promise.resolve(channel);
  pump();
})();
)JS";

QString mintToken()
{
    // 128 bits from the system generator, hex. Long enough that guessing it is
    // not a strategy, short enough to read in a log.
    quint64 hi = QRandomGenerator::system()->generate64();
    quint64 lo = QRandomGenerator::system()->generate64();
    return QStringLiteral("%1%2")
        .arg(hi, 16, 16, QLatin1Char('0'))
        .arg(lo, 16, 16, QLatin1Char('0'));
}

QUrl originUrl(const QString& path)
{
    QUrl url;
    url.setScheme(QLatin1String(kSchemeName));
    url.setHost(QLatin1String(kModuleHost));
    url.setPath(path);
    return url;
}

BridgeReply refusal(int status, const char* why)
{
    BridgeReply reply;
    reply.status = status;
    reply.mimeType = QByteArrayLiteral("text/plain");
    reply.body = QByteArray(why);
    return reply;
}

} // namespace

MobileWebBridge::MobileWebBridge(QString moduleDir, QString runtimeDir, QString entryFile)
    : m_moduleDir(std::move(moduleDir))
    , m_runtimeDir(std::move(runtimeDir))
    , m_entryFile(std::move(entryFile))
    , m_token(mintToken())
{
}

MobileWebBridge::~MobileWebBridge()
{
    close();
}

QUrl MobileWebBridge::entryUrl() const
{
    QString file = m_entryFile;
    while (file.startsWith(QLatin1Char('/'))) file.remove(0, 1);
    if (file.isEmpty()) file = QStringLiteral("index.html");
    return originUrl(QStringLiteral("/") + file);
}

QString MobileWebBridge::channelShim() const
{
    QString shim = QString::fromUtf8(kShim);
    shim.replace(QStringLiteral("@RUNTIME_BASE@"),
                 originUrl(QLatin1String(kRuntimePathPrefix)).toString());
    shim.replace(QStringLiteral("@CONTROL_BASE@"),
                 originUrl(QLatin1String(kControlPathPrefix) + m_token).toString());
    return shim;
}

BridgeReply MobileWebBridge::closedReply()
{
    BridgeReply reply;
    reply.mimeType = QByteArrayLiteral("application/json");
    reply.body = QByteArrayLiteral("{\"frames\":[],\"closed\":true}");
    return reply;
}

BridgeReply MobileWebBridge::framesReply(const std::vector<std::string>& frames)
{
    QJsonArray array;
    for (const std::string& frame : frames) array.append(QString::fromStdString(frame));
    QJsonObject object;
    object.insert(QStringLiteral("frames"), array);

    BridgeReply reply;
    reply.mimeType = QByteArrayLiteral("application/json");
    reply.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return reply;
}

BridgeReply MobileWebBridge::drainLocked()
{
    std::vector<std::string> frames(m_outbound.begin(), m_outbound.end());
    m_outbound.clear();
    return framesReply(frames);
}

void MobileWebBridge::handleRequest(const QByteArray& method, const QUrl& url,
                                    const QByteArray& body, Respond respond)
{
    if (!respond) return;

    if (url.host() != QLatin1String(kModuleHost)) {
        respond(refusal(404, "not this origin"));
        return;
    }

    const QString path = url.path();
    const QLatin1String controlPrefix(kControlPathPrefix);

    if (!path.startsWith(controlPrefix)) {
        // A DOCUMENT. The rules are LogosWebPaths', shared with the desktop
        // container, so a package that loads in one loads in all three.
        const QString resolved = resolveDocument(m_moduleDir, m_runtimeDir, path);
        if (resolved.isEmpty()) {
            respond(refusal(404, "no such file in this package"));
            return;
        }
        BridgeReply reply;
        reply.mimeType = mimeTypeFor(resolved);
        reply.filePath = resolved;
        respond(reply);
        return;
    }

    // `/__logos/<token>/<leaf>`, and nothing else.
    const QString rest = path.mid(controlPrefix.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    const QString token = slash < 0 ? rest : rest.left(slash);
    const QString leaf = slash < 0 ? QString() : rest.mid(slash + 1);

    // CONSTANT-TIME IS NOT THE POINT and a comparison here is not the attack
    // surface — a page that can time this can already POST to `send`. What the
    // check buys is that a document loaded into this webview by some other
    // route cannot speak as the module merely by knowing the scheme.
    if (token != m_token) {
        respond(refusal(403, "not this launch's token"));
        return;
    }

    if (leaf == QLatin1String("send")) {
        if (method != QByteArrayLiteral("POST")) {
            respond(refusal(405, "send is a POST"));
            return;
        }
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (!m_open) {
                respond(refusal(410, "this channel is closed"));
                return;
            }
        }
        // Delivered OUTSIDE m_mutex: a receiver reaching back into send() under
        // it would deadlock, and the peer above does exactly that when it
        // answers a Call inline.
        Receiver receiver;
        {
            std::lock_guard<std::recursive_mutex> guard(m_receiverMutex);
            receiver = m_receiver;
        }
        if (receiver) receiver(std::string(body.constData(), size_t(body.size())));

        BridgeReply reply;
        reply.mimeType = QByteArrayLiteral("application/json");
        reply.body = QByteArrayLiteral("{\"ok\":true}");
        respond(reply);
        return;
    }

    if (leaf == QLatin1String("poll")) {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_open) {
            respond(closedReply());
            return;
        }
        if (!m_outbound.empty()) {
            respond(drainLocked());
            return;
        }
        // PARKED. Answered by the next send(), by expireWaits(), or by close().
        m_waiting.push_back(std::move(respond));
        return;
    }

    if (leaf == QLatin1String("close")) {
        if (method != QByteArrayLiteral("POST")) {
            respond(refusal(405, "close is a POST"));
            return;
        }
        BridgeReply reply;
        reply.mimeType = QByteArrayLiteral("application/json");
        reply.body = QByteArrayLiteral("{\"ok\":true}");
        respond(reply);

        std::function<void()> announce;
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (m_open) announce = m_onPageClosed;
        }
        close();
        if (announce) announce();
        return;
    }

    respond(refusal(404, "no such control path"));
}

void MobileWebBridge::setReceiver(Receiver receiver)
{
    std::lock_guard<std::recursive_mutex> guard(m_receiverMutex);
    m_receiver = std::move(receiver);
}

bool MobileWebBridge::send(const std::string& frame)
{
    Respond waiting;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_open) return false;
        m_outbound.push_back(frame);
        if (!m_waiting.empty()) {
            waiting = std::move(m_waiting.front());
            m_waiting.erase(m_waiting.begin());
        }
    }
    if (!waiting) return true;

    // Drained under the lock again rather than from the copy above: another
    // send() may have arrived between the two, and a batch that left a frame
    // behind would deliver it out of order on the next poll.
    BridgeReply reply;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        reply = drainLocked();
    }
    waiting(reply);
    return true;
}

void MobileWebBridge::close()
{
    std::vector<Respond> waiting;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_open) return;
        m_open = false;
        m_outbound.clear();
        waiting.swap(m_waiting);
    }
    // EVERY PARKED POLL IS ANSWERED. A fetch the host never completes is a
    // promise the page's loader waits on forever, while the container has
    // already decided the module is gone.
    for (Respond& respond : waiting) respond(closedReply());
}

bool MobileWebBridge::isOpen() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_open;
}

void MobileWebBridge::setOnPageClosed(std::function<void()> callback)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_onPageClosed = std::move(callback);
}

void MobileWebBridge::expireWaits()
{
    std::vector<Respond> waiting;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_open) return;
        waiting.swap(m_waiting);
    }
    const BridgeReply empty = framesReply({});
    for (Respond& respond : waiting) respond(empty);
}

} // namespace basecamp::web
