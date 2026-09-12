#include "web/WebModulePageView.h"
#include "web/LogosWebScheme.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QUrl>
#include <QWebChannel>
#include <QWebEngineProfile>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <mutex>

namespace basecamp::web {

// ── the bridge ──────────────────────────────────────────────────────────────

void PageBridge::deliverToPage(const QString& text)
{
    if (!m_pageReady) { m_backlog.append(text); return; }
    emit toPage(text);
}

void PageBridge::ready()
{
    if (m_pageReady) return;
    m_pageReady = true;
    const QStringList backlog = m_backlog;
    m_backlog.clear();
    for (const QString& text : backlog) emit toPage(text);
}

// ── the channel ─────────────────────────────────────────────────────────────

// The web transport's endpoint on this page.
//
// send() HOPS TO THE QT MAIN THREAD and returns. IMessageChannel's contract
// says delivery is asynchronous and never inline — RpcPeer writes with its
// registry mutex held, and a channel that delivered inline would re-enter it —
// and a QWebChannel signal may only be emitted where the bridge lives anyway.
// The bridge is the invokeMethod CONTEXT object, so a message posted while the
// page is being torn down is dropped by Qt rather than delivered to a destroyed
// object.
class PageChannel : public logos::web::IMessageChannel {
public:
    explicit PageChannel(PageBridge* bridge) : m_bridge(bridge) {}

    void setReceiver(Receiver receiver) override
    {
        std::lock_guard<std::recursive_mutex> g(m_receiverMu);
        m_receiver = std::move(receiver);
    }

    // Called on the Qt main thread, from PageBridge::fromPage.
    void deliver(const std::string& text)
    {
        // COPIED, then invoked: a receiver may detach itself mid-delivery, and
        // calling the member would run a std::function setReceiver has just
        // destroyed.
        std::lock_guard<std::recursive_mutex> g(m_receiverMu);
        const Receiver cb = m_receiver;
        if (cb) cb(text);
    }

    bool send(const std::string& message) override
    {
        PageBridge* bridge = nullptr;
        {
            std::lock_guard<std::mutex> g(m_mu);
            if (m_closed) return false;
            bridge = m_bridge;
        }
        if (!bridge) return false;
        const QString text = QString::fromStdString(message);
        QMetaObject::invokeMethod(bridge, [bridge, text] { bridge->deliverToPage(text); },
                                  Qt::QueuedConnection);
        return true;
    }

    void close() override
    {
        std::lock_guard<std::mutex> g(m_mu);
        m_closed = true;
        m_bridge = nullptr;
    }

    bool isOpen() const override
    {
        std::lock_guard<std::mutex> g(m_mu);
        return !m_closed;
    }

private:
    mutable std::mutex m_mu;
    bool m_closed = false;
    PageBridge* m_bridge = nullptr;   // cleared by close(); QPointer is not
                                      // thread-safe, so the mutex does the job

    std::recursive_mutex m_receiverMu;
    Receiver m_receiver;
};

namespace {

// The shim, injected at document creation in the main world.
//
// It runs BEFORE the page's own scripts, which is what lets a variant's entry
// document read `window.logosQmlRuntimeBase` at the top of its first module
// script and `await window.logosChannelReady` in its loader. The QWebChannel
// handshake is asynchronous, so the PROMISE — not the channel — is what can be
// published synchronously.
//
// `@RUNTIME_BASE@` is substituted by the view below. The rest is
// logoscore-webhost's shim, deliberately: a module page written against the
// browser SDK must behave identically in both desktop containers, and the way
// to guarantee that is for both to hand it the same five methods with the same
// queueing rules.
const char* kChannelShim = R"JS(
(function () {
  window.logosQmlRuntimeBase = '@RUNTIME_BASE@';

  var pending = [];
  var receiver = null;
  var open = true;
  var bridge = null;

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

  var channel = {
    send: function (text) {
      if (!open || !bridge) return false;
      bridge.toHost(String(text));
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
      if (bridge) bridge.closed();
    },
    isOpen: function () { return open; }
  };

  window.logosChannelReady = new Promise(function (resolve, reject) {
    function build() {
      try {
        new QWebChannel(qt.webChannelTransport, function (ch) {
          bridge = ch.objects.logos;
          bridge.toPage.connect(deliver);
          // Only now can a signal from the host reach this page, so only now
          // may the host stop holding them. See PageBridge::ready().
          bridge.ready();
          resolve(channel);
        });
      } catch (e) {
        reject(e);
      }
    }
    if (typeof qt !== 'undefined' && qt.webChannelTransport) build();
    else document.addEventListener('DOMContentLoaded', build);
  });
})();
)JS";

QString qtResource(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}

// A page whose script throws is the hardest failure to diagnose from the
// outside: the container sees "the page never published a module" and nothing
// else, because a page that died on line one is indistinguishable from one that
// simply has no module in it. So the console goes to the app's log.
class LoggingPage : public QWebEnginePage {
public:
    LoggingPage(QWebEngineProfile* profile, QString label, QObject* parent)
        : QWebEnginePage(profile, parent), m_label(std::move(label)) {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level,
                                  const QString& message,
                                  int lineNumber,
                                  const QString& sourceID) override
    {
        const QString line = QStringLiteral("[web:%1] %2:%3 %4")
                                 .arg(m_label, sourceID, QString::number(lineNumber), message);
        // qInfo rather than qDebug for the ordinary levels: a page that is
        // failing to start says so in the console and nowhere else, and qDebug
        // is the one severity a host is likely to have filtered off.
        if (level == ErrorMessageLevel) qWarning().noquote() << line;
        else                            qInfo().noquote()    << line;
    }

private:
    QString m_label;
};

} // namespace

// ── the view ────────────────────────────────────────────────────────────────

WebModulePageView::WebModulePageView(const LogosCore::WebModuleViewRequest& request,
                                     const QString& runtimeDir)
    : m_moduleName(QString::fromStdString(request.moduleName))
{
    const QString entry = QString::fromStdString(request.entryPath);
    const QString moduleDir = QString::fromStdString(request.moduleDir);

    const QString webChannelJs = qtResource(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
    if (webChannelJs.isEmpty()) {
        m_startupError = QStringLiteral("qwebchannel.js is not in this Qt build");
        qWarning() << "Web module" << m_moduleName << ":" << m_startupError;
        return;
    }

    // A PROFILE PER MODULE, off the record. Nothing one module's page stores is
    // reachable from another's — the storage half of the identity separation the
    // container gets structurally from one channel per view — and it is also
    // what lets `logos://module/` mean a different directory in each page.
    m_profile = new QWebEngineProfile(this);
    m_handler = new LogosWebSchemeHandler(moduleDir, runtimeDir, m_profile);
    m_profile->installUrlSchemeHandler(QByteArray(kSchemeName), m_handler);

    m_bridge = new PageBridge(this);
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject(QStringLiteral("logos"), m_bridge);

    m_channel = std::make_shared<PageChannel>(m_bridge);
    auto channel = m_channel;
    connect(m_bridge, &PageBridge::fromPage, this, [channel](const QString& text) {
        channel->deliver(text.toStdString());
    });
    // A page closing its channel is a module reporting that it has stopped
    // serving — a trapped wasm image does exactly this. Same verdict as a dead
    // renderer, because it is the same fact.
    connect(m_bridge, &PageBridge::pageClosed, this, [this] {
        qWarning() << "Web module" << m_moduleName
                   << "closed its channel; it is no longer serving";
        announceDeath();
    });

    m_view = new QWebEngineView();
    auto* page = new LoggingPage(m_profile, m_moduleName, m_view);
    m_view->setPage(page);
    page->setWebChannel(m_webChannel);
    // The variant's loader instantiates two wasm images and draws with WebGL;
    // without this the runtime comes up and renders nothing.
    page->settings()->setAttribute(QWebEngineSettings::WebGLEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::Accelerated2dCanvasEnabled, true);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    page->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);

    QString shim = QString::fromUtf8(kChannelShim);
    shim.replace(QStringLiteral("@RUNTIME_BASE@"),
                 QStringLiteral("%1://%2%3")
                     .arg(QLatin1String(kSchemeName), QLatin1String(kModuleHost),
                          QLatin1String(kRuntimePathPrefix)));

    QWebEngineScript script;
    script.setName(QStringLiteral("logos-web-channel"));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(webChannelJs + shim);
    page->scripts().insert(script);

    connect(page, &QWebEnginePage::renderProcessTerminated, this,
            [this](QWebEnginePage::RenderProcessTerminationStatus status, int exitCode) {
                qWarning() << "Web module" << m_moduleName
                           << "lost its renderer (status" << int(status)
                           << "exit" << exitCode << ")";
                announceDeath();
            });
    connect(page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        if (ok) qInfo() << "Web module" << m_moduleName << "loaded its entry document";
        else    qWarning() << "Web module" << m_moduleName
                           << "failed to load its entry document";
    });

    // THE ENTRY DOCUMENT, BY ITS PATH INSIDE THE PACKAGE. `desc.path` is an
    // absolute file path and `moduleDir` is its parent, so the page's URL is
    // the file's name under the origin this view serves that directory on. A
    // file:// URL would be the same bytes and a different origin — one that may
    // not fetch its own QML (ADR 0004).
    const QString file = QDir(moduleDir).relativeFilePath(entry);
    QUrl url;
    url.setScheme(QLatin1String(kSchemeName));
    url.setHost(QLatin1String(kModuleHost));
    url.setPath(QStringLiteral("/") + file);
    m_view->load(url);
}

WebModulePageView::~WebModulePageView()
{
    // The deliberate teardown, so the death callback must NOT fire: the
    // container is already unloading this module and announcing here would
    // report an orderly unload as a lost page.
    m_announced = true;
    m_alive = false;
    if (m_channel) m_channel->close();
    if (m_view) delete m_view.data();
}

logos::web::MessageChannelPtr WebModulePageView::channel() const
{
    return m_channel;
}

void WebModulePageView::setOnDied(std::function<void()> callback)
{
    m_onDied = std::move(callback);
}

bool WebModulePageView::isAlive() const
{
    return m_alive;
}

QWidget* WebModulePageView::widget() const
{
    return m_view.data();
}

void WebModulePageView::announceDeath()
{
    if (m_announced) return;
    m_announced = true;
    m_alive = false;
    if (m_channel) m_channel->close();
    if (m_onDied) m_onDied();
}

} // namespace basecamp::web
