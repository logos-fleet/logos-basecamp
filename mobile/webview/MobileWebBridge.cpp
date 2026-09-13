#include "webview/MobileWebBridge.h"

#include "web/LogosWebPaths.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QFile>
#include <QRandomGenerator>
#include <QUrlQuery>

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
  var CHUNK = @CHUNK_CHARS@;

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

  // ONE FRAME, IN THE URL, IN PIECES. Neither phone's interceptor is handed a
  // request body (see the header), so the frame is percent-encoded and split
  // across as many requests as it takes. Chunks of one frame are sent in order
  // and the host assembles by sequence number, so two frames in flight cannot
  // interleave.
  var nextSeq = 0;

  // EVERY CHUNK IS A WHOLE ENCODING, NEVER A SLICE OF ONE.
  //
  // The host decodes each chunk on arrival and concatenates the results, so a
  // chunk has to be decodable BY ITSELF. Encoding the frame once and then
  // cutting the result every CHUNK characters is not: a percent-escape is three
  // characters (`%22` for a quote, nine for an em dash) and a boundary landing
  // inside one produces two pieces that decode to something neither half meant.
  // The frame the host then delivers is the right shape and the wrong bytes,
  // the peer drops it without a word, and the container reports "the page never
  // published a module" about a page that answered perfectly. It took until a
  // frame that was not all letters to show: 30000 `z` characters encode to
  // themselves, so no cut could land inside an escape, and JSON -- where `{`,
  // `"`, `,` and `:` are each three characters on the wire -- is the shape that
  // finds it.
  //
  // So the frame is encoded a code point at a time and the pieces are packed
  // into chunks, each still inside CHUNK characters. `codePointAt` rather than
  // an index walk because a surrogate pair encodes as one four-byte sequence
  // and must not be split either.
  function splitEncoded(text) {
    var parts = [];
    var cur = '';
    for (var i = 0; i < text.length; ) {
      var ch = String.fromCodePoint(text.codePointAt(i));
      i += ch.length;
      var enc = encodeURIComponent(ch);
      if (cur.length > 0 && cur.length + enc.length > CHUNK) {
        parts.push(cur);
        cur = '';
      }
      cur += enc;
    }
    parts.push(cur);
    return parts;
  }

  function post(text) {
    var parts = splitEncoded(String(text));
    var count = parts.length;
    var seq = String(nextSeq++);
    // SAID OUT LOUD WHEN IT SPLITS, because a chunked frame is the one shape
    // this path can get wrong in a way nothing downstream reports: the host
    // reassembles by sequence number and a frame that arrives short is not a
    // frame at all, it is silence.
    if (count > 1) {
      var encodedChars = 0;
      for (var j = 0; j < count; j++) encodedChars += parts[j].length;
      console.log('logos-bridge: frame ' + encodedChars
                  + ' encoded chars -> ' + count + ' chunks (seq ' + seq + ')');
    }
    var chain = Promise.resolve();
    for (var i = 0; i < count; i++) {
      (function (index) {
        chain = chain.then(function () {
          return fetch(CONTROL + '/send?s=' + seq + '&i=' + index + '&n=' + count
                       + '&d=' + parts[index],
                       { method: 'GET', cache: 'no-store' });
        });
      })(i);
    }
    return chain;
  }

  var channel = {
    send: function (text) {
      if (!open) return false;
      post(text).catch(function () { /* the page is being torn down */ });
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
      fetch(CONTROL + '/close', { method: 'GET', cache: 'no-store' }).catch(function () {});
    },
    isOpen: function () { return open; }
  };

  // THE CONSOLE, DOWN THE SAME SCHEME. There is no WKScriptMessageHandler to
  // route it through (it traps under Qt's main stack) and Android's
  // WebChromeClient would only cover one of the two phones, so the page reports
  // its own console and both containers log it the same way. Wrapped rather
  // than replaced: a developer with Safari's inspector attached still sees
  // everything.
  ['log', 'info', 'warn', 'error'].forEach(function (level) {
    var original = console[level] ? console[level].bind(console) : function () {};
    console[level] = function () {
      var parts = [];
      for (var i = 0; i < arguments.length; i++) {
        var a = arguments[i];
        try { parts.push(typeof a === 'string' ? a : JSON.stringify(a)); }
        catch (e) { parts.push(String(a)); }
      }
      fetch(CONTROL + '/log?l=' + level + '&m=' + encodeURIComponent(parts.join(' ')),
            { method: 'GET', cache: 'no-store' }).catch(function () {});
      original.apply(console, arguments);
    };
  });
  window.addEventListener('error', function (e) {
    console.error('uncaught: ' + (e && e.message) + ' at ' + (e && e.filename)
                  + ':' + (e && e.lineno));
  });

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

BridgeReply refusal(int status, const char* why)
{
    BridgeReply reply;
    reply.status = status;
    reply.mimeType = QByteArrayLiteral("text/plain");
    reply.body = QByteArray(why);
    return reply;
}

BridgeReply jsonReply(const QByteArray& body)
{
    BridgeReply reply;
    reply.mimeType = QByteArrayLiteral("application/json");
    reply.body = body;
    return reply;
}

// What every control path that has nothing to report answers with.
BridgeReply ackReply()
{
    return jsonReply(QByteArrayLiteral("{\"ok\":true}"));
}

} // namespace

MobileWebBridge::MobileWebBridge(QString moduleDir, QString runtimeDir, QString entryFile,
                                 WebOrigin origin)
    : m_moduleDir(std::move(moduleDir))
    , m_runtimeDir(std::move(runtimeDir))
    , m_entryFile(std::move(entryFile))
    , m_origin(std::move(origin))
    , m_token(mintToken())
{
}

MobileWebBridge::~MobileWebBridge()
{
    close();
}

QUrl MobileWebBridge::entryUrl() const
{
    return documentUrl(m_entryFile);
}

QUrl MobileWebBridge::documentUrl(const QString& file) const
{
    QString name = file;
    while (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    if (name.isEmpty()) name = QStringLiteral("index.html");
    return m_origin.url(QStringLiteral("/") + name);
}

QString MobileWebBridge::channelShim() const
{
    QString shim = QString::fromUtf8(kShim);
    shim.replace(QStringLiteral("@RUNTIME_BASE@"),
                 m_origin.url(QLatin1String(kRuntimePathPrefix)).toString());
    shim.replace(QStringLiteral("@CONTROL_BASE@"),
                 m_origin.url(QLatin1String(kControlPathPrefix) + m_token).toString());
    shim.replace(QStringLiteral("@CHUNK_CHARS@"), QString::number(kChunkChars));
    return shim;
}

BridgeReply MobileWebBridge::closedReply()
{
    return jsonReply(QByteArrayLiteral("{\"frames\":[],\"closed\":true}"));
}

BridgeReply MobileWebBridge::framesReply(const std::vector<std::string>& frames)
{
    QJsonArray array;
    for (const std::string& frame : frames) array.append(QString::fromStdString(frame));
    QJsonObject object;
    object.insert(QStringLiteral("frames"), array);
    return jsonReply(QJsonDocument(object).toJson(QJsonDocument::Compact));
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

    if (url.host() != m_origin.host || url.scheme() != m_origin.scheme) {
        respond(refusal(404, "not this origin"));
        return;
    }

    if (method != QByteArrayLiteral("GET") && method != QByteArrayLiteral("POST")) {
        respond(refusal(405, "GET or POST"));
        return;
    }

    const QString path = url.path();
    const QLatin1String controlPrefix(kControlPathPrefix);
    if (!path.startsWith(controlPrefix)) {
        serveDocument(path, respond);
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
        handleSend(url, body, respond);
    } else if (leaf == QLatin1String("poll")) {
        handlePoll(std::move(respond));
    } else if (leaf == QLatin1String("log")) {
        handleLog(url, respond);
    } else if (leaf == QLatin1String("close")) {
        handleClose(respond);
    } else {
        respond(refusal(404, "no such control path"));
    }
}

// A DOCUMENT. The rules are LogosWebPaths', shared with the desktop container,
// so a package that loads in one loads in all three.
void MobileWebBridge::serveDocument(const QString& path, const Respond& respond)
{
    const QString resolved = resolveDocument(m_moduleDir, m_runtimeDir, path);
    if (resolved.isEmpty()) {
        respond(refusal(404, "no such file in this package"));
        return;
    }

    BridgeReply reply;
    reply.mimeType = mimeTypeFor(resolved);
    if (m_injectShim && reply.mimeType == QByteArrayLiteral("text/html")) {
        QFile file(resolved);
        if (!file.open(QIODevice::ReadOnly)) {
            respond(refusal(500, "the entry document could not be read"));
            return;
        }
        reply.body = withShim(file.readAll());
    } else {
        reply.filePath = resolved;
    }
    respond(reply);
}

void MobileWebBridge::handleSend(const QUrl& url, const QByteArray& body,
                                 const Respond& respond)
{
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!m_open) {
            respond(refusal(410, "this channel is closed"));
            return;
        }
    }

    // Either spelling: a body when the platform hands us one, or the chunked
    // query when it does not (see the header -- neither phone does).
    QString frame;
    bool complete = false;
    if (!body.isEmpty()) {
        frame = QString::fromUtf8(body);
        complete = true;
    } else {
        const QUrlQuery query(url);
        const QString seq = query.queryItemValue(QStringLiteral("s"));
        bool okIndex = false, okCount = false;
        const int index = query.queryItemValue(QStringLiteral("i")).toInt(&okIndex);
        const int count = query.queryItemValue(QStringLiteral("n")).toInt(&okCount);
        const QString chunk = query.queryItemValue(QStringLiteral("d"), QUrl::FullyDecoded);
        if (seq.isEmpty() || !okIndex || !okCount || count <= 0
            || index < 0 || index >= count) {
            respond(refusal(400, "send needs s, i, n and d"));
            return;
        }

        std::lock_guard<std::mutex> guard(m_mutex);
        Partial& partial = m_partial[seq];
        if (partial.count != count) {
            partial.count = count;
            partial.chunks.assign(size_t(count), QString());
            partial.have = 0;
        }
        // A REPEATED CHUNK IS NOT A SECOND ONE. A webview that retried a
        // request would otherwise complete the frame early and deliver it with
        // a hole in it.
        if (partial.chunks[size_t(index)].isNull()) ++partial.have;
        partial.chunks[size_t(index)] = chunk;
        if (partial.have == count) {
            for (const QString& piece : partial.chunks) frame += piece;
            m_partial.erase(seq);
            complete = true;
            if (count > 1)
                qInfo() << "Web bridge: reassembled a" << frame.size()
                        << "char frame from" << count << "chunks";
        }
    }

    if (complete) {
        // Delivered OUTSIDE m_mutex: a receiver reaching back into send() under
        // it would deadlock, and the peer above does exactly that when it
        // answers a Call inline.
        Receiver receiver;
        std::function<void(const QString&)> observer;
        {
            std::lock_guard<std::recursive_mutex> guard(m_receiverMutex);
            receiver = m_receiver;
            observer = m_observer;
        }
        // The observer FIRST, and it is not arbitrary: the receiver may answer
        // inline and produce more traffic, and an observer that saw the reply
        // before the frame that caused it would read backwards.
        if (observer) observer(frame);
        if (receiver) receiver(frame.toStdString());
    }

    respond(complete ? ackReply()
                     : jsonReply(QByteArrayLiteral("{\"ok\":true,\"partial\":true}")));
}

void MobileWebBridge::handlePoll(Respond respond)
{
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
}

void MobileWebBridge::handleLog(const QUrl& url, const Respond& respond)
{
    std::function<void(const QString&, const QString&)> sink;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        sink = m_onPageLog;
    }
    if (sink) {
        const QUrlQuery query(url);
        sink(query.queryItemValue(QStringLiteral("l")),
             query.queryItemValue(QStringLiteral("m"), QUrl::FullyDecoded));
    }
    respond(ackReply());
}

void MobileWebBridge::handleClose(const Respond& respond)
{
    respond(ackReply());

    std::function<void()> announce;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_open) announce = m_onPageClosed;
    }
    close();
    if (announce) announce();
}

void MobileWebBridge::setInjectsShimIntoHtml(bool injects)
{
    m_injectShim = injects;
}

QByteArray MobileWebBridge::withShim(const QByteArray& html) const
{
    const QByteArray tag = QByteArrayLiteral("<script>") + channelShim().toUtf8()
                           + QByteArrayLiteral("</script>");
    // AS EARLY AS THE DOCUMENT ALLOWS. After <head> when there is one, before
    // everything when there is not -- a shim that landed after the page's own
    // first script would be exactly as late as evaluateJavascript, which is why
    // this exists at all.
    const QByteArray headOpen = QByteArrayLiteral("<head>");
    const int head = html.indexOf(headOpen);
    if (head < 0) return tag + html;

    QByteArray out = html;
    out.insert(head + headOpen.size(), tag);
    return out;
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

void MobileWebBridge::setFrameObserver(std::function<void(const QString&)> observer)
{
    std::lock_guard<std::recursive_mutex> guard(m_receiverMutex);
    m_observer = std::move(observer);
}

void MobileWebBridge::setOnPageClosed(std::function<void()> callback)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_onPageClosed = std::move(callback);
}

void MobileWebBridge::setOnPageLog(
    std::function<void(const QString&, const QString&)> callback)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_onPageLog = std::move(callback);
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
