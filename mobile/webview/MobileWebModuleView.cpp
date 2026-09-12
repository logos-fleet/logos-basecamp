#include "webview/MobileWebModuleView.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace basecamp::web {

namespace {

// THE HEADLESS ENTRY DOCUMENT THIS PACKAGE SHIPS, or empty when it ships none.
//
// Read out of the package's own manifest rather than assumed, because the file
// name is the BUILDER's and this is the container: logos-module-builder writes
// `logos_web_view.headless` next to the qml document and the backend glue it
// already names there, and a package built before that key existed simply has
// no headless document -- which a container must be able to discover, since the
// only other answer to an eviction is to unload the module.
QString headlessEntryOf(const QString& moduleDir)
{
    QFile manifest(QDir(moduleDir).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(manifest.readAll()).object();
    const QString entry = root.value(QStringLiteral("logos_web_view"))
                              .toObject()
                              .value(QStringLiteral("headless"))
                              .toString();
    if (entry.isEmpty()) return {};
    // IT HAS TO BE IN THE PACKAGE. A manifest naming a document that is not
    // there would turn every eviction into a page that loads nothing, and the
    // module would go quiet with no error anywhere.
    return QFile::exists(QDir(moduleDir).filePath(entry)) ? entry : QString();
}

// The web transport's endpoint on a phone's page, over the bridge.
//
// Thin on purpose: the queueing, the buffering and the closing rules are all
// MobileWebBridge's, because they are what a desktop test can drive. This is
// the shape logos-protocol asks for, wrapped around them.
class BridgeChannel : public logos::web::IMessageChannel {
public:
    explicit BridgeChannel(std::shared_ptr<MobileWebBridge> bridge)
        : m_bridge(std::move(bridge)) {}

    void setReceiver(Receiver receiver) override
    {
        if (m_bridge) m_bridge->setReceiver(std::move(receiver));
    }

    bool send(const std::string& message) override
    {
        return m_bridge && m_bridge->send(message);
    }

    void close() override
    {
        if (m_bridge) m_bridge->close();
    }

    bool isOpen() const override
    {
        return m_bridge && m_bridge->isOpen();
    }

private:
    std::shared_ptr<MobileWebBridge> m_bridge;
};

} // namespace

MobileWebModuleView::MobileWebModuleView(const LogosCore::WebModuleViewRequest& request,
                                         const QString& runtimeDir,
                                         const PlatformPageFactory& platform,
                                         bool shimInDocument,
                                         WebOrigin origin)
    : m_moduleName(QString::fromStdString(request.moduleName))
{
    const QString moduleDir = QString::fromStdString(request.moduleDir);
    const QString entry = QString::fromStdString(request.entryPath);
    // The entry's name INSIDE the package: `entryPath` is absolute and
    // `moduleDir` is its parent, so the page's URL is that name under the origin
    // this view serves the directory on. A file:// URL would be the same bytes
    // and a different origin — one that may not fetch its own QML (ADR 0004).
    const QString entryFile = QDir(moduleDir).relativeFilePath(entry);

    if (!platform) {
        m_startupError = QStringLiteral("this build installed no webview backend");
        qWarning() << "Web module" << m_moduleName << ":" << m_startupError;
        return;
    }

    m_platform = platform;
    m_shimInDocument = shimInDocument;
    m_uiEntry = entryFile;
    m_headlessEntry = headlessEntryOf(moduleDir);
    if (m_headlessEntry.isEmpty()) {
        qInfo() << "Web module" << m_moduleName
                << "ships no headless entry document; an eviction will have to "
                   "unload it rather than background it";
    }

    m_bridge = std::make_shared<MobileWebBridge>(moduleDir, runtimeDir, entryFile,
                                                std::move(origin));
    m_bridge->setInjectsShimIntoHtml(shimInDocument);
    m_channel = std::make_shared<BridgeChannel>(m_bridge);

    // THE PAGE'S OWN CONSOLE, out where a device run can read it. A `web`
    // variant draws into a canvas: from outside there is no DOM to query and no
    // label to read, so what the module says about itself is the only window
    // into whether its view came up. The desktop container routes it into Qt's
    // message handler for the same reason; this is the phones' half of that.
    m_bridge->setOnPageLog([this](const QString& level, const QString& message) {
        const QString line = QStringLiteral("[web %1] %2").arg(m_moduleName, message);
        if (level == QLatin1String("error")) qWarning().noquote() << line;
        else qInfo().noquote() << line;
        if (m_onPageLog) m_onPageLog(level, message);
    });

    // A page closing its channel is a module reporting that it has stopped
    // serving — a trapped wasm image does exactly this. Same verdict as a dead
    // webview, because it is the same fact.
    m_bridge->setOnPageClosed([this] {
        qWarning() << "Web module" << m_moduleName
                   << "closed its channel; it is no longer serving";
        announceDeath();
    });

    if (!openPage(m_uiEntry)) {
        m_startupError = QStringLiteral("the platform could not open a webview");
        qWarning() << "Web module" << m_moduleName << ":" << m_startupError;
        m_bridge->close();
    }
}

bool MobileWebModuleView::openPage(const QString& entryFile)
{
    PlatformPageRequest pageRequest;
    pageRequest.moduleName = m_moduleName;
    pageRequest.entryUrl = m_bridge->documentUrl(entryFile);
    // EMPTY when the document carries it: a platform that injects AND a
    // container that serves it inside the HTML would run the shim twice, and
    // the second one would take over the channel the first had already opened.
    pageRequest.channelShim = m_shimInDocument ? QString() : m_bridge->channelShim();
    auto bridge = m_bridge;
    pageRequest.serve = [bridge](const QByteArray& method, const QUrl& url,
                                 const QByteArray& body, MobileWebBridge::Respond respond) {
        bridge->handleRequest(method, url, body, std::move(respond));
    };
    pageRequest.onDied = [this] {
        // DURING A SWAP THIS IS US. Both platforms detach their callbacks
        // before tearing a webview down, but a report already in flight on
        // another thread would otherwise turn an eviction into a lost module.
        if (m_swapping) return;
        qWarning() << "Web module" << m_moduleName << "lost its webview";
        announceDeath();
    };

    m_page = m_platform(pageRequest);
    return bool(m_page.destroy);
}

bool MobileWebModuleView::swapPageTo(const QString& entryFile)
{
    if (!m_bridge || !m_platform || !m_alive) return false;

    m_swapping = true;
    if (m_page.destroy) m_page.destroy();
    m_page = PlatformPage();
    // THE POLLS THE OUTGOING PAGE HAD PARKED. Android answers a poll by
    // blocking the thread that asked, and a thread still parked on a webview
    // that no longer exists is one the next page cannot use. Answering them
    // with an empty batch costs nothing -- the page they belong to is gone --
    // and the incoming one arms its own.
    m_bridge->expireWaits();
    const bool opened = openPage(entryFile);
    m_swapping = false;
    if (!opened) {
        qWarning() << "Web module" << m_moduleName << "could not open" << entryFile;
        announceDeath();
    }
    return opened;
}

bool MobileWebModuleView::evictUi()
{
    if (!m_hasUi) return true;
    if (m_headlessEntry.isEmpty()) return false;
    if (!swapPageTo(m_headlessEntry)) return false;
    m_hasUi = false;
    qInfo() << "Web module" << m_moduleName
            << "gave its UI page up; its Wasm host is on" << m_headlessEntry;
    return true;
}

bool MobileWebModuleView::restoreUi()
{
    if (m_hasUi) return true;
    if (!swapPageTo(m_uiEntry)) return false;
    m_hasUi = true;
    qInfo() << "Web module" << m_moduleName << "has its UI page back";
    return true;
}

void MobileWebModuleView::observeFrames(std::function<void(const QString&)> sink)
{
    if (m_bridge) m_bridge->setFrameObserver(std::move(sink));
}

bool MobileWebModuleView::sendFrame(const QString& frame)
{
    return m_bridge && m_bridge->send(frame.toStdString());
}

bool MobileWebModuleView::runJavaScript(const QString& script)
{
    if (!m_page.evaluateJavaScript) return false;
    m_page.evaluateJavaScript(script);
    return true;
}

MobileWebModuleView::~MobileWebModuleView()
{
    if (m_onDestroyed) m_onDestroyed();

    // The deliberate teardown, so the death callback must NOT fire: the
    // container is already unloading this module and announcing here would
    // report an orderly unload as a lost page.
    m_announced = true;
    m_alive = false;
    if (m_bridge) {
        m_bridge->setOnPageClosed(nullptr);
        m_bridge->close();
    }
    if (m_page.destroy) m_page.destroy();
}

logos::web::MessageChannelPtr MobileWebModuleView::channel() const
{
    return m_channel;
}

void MobileWebModuleView::setOnDied(std::function<void()> callback)
{
    m_onDied = std::move(callback);
}

bool MobileWebModuleView::isAlive() const
{
    if (!m_alive) return false;
    return m_page.isAlive ? m_page.isAlive() : true;
}

void* MobileWebModuleView::nativeHandle() const
{
    return m_page.nativeHandle ? m_page.nativeHandle() : nullptr;
}

void MobileWebModuleView::setFrontmost(bool front)
{
    if (m_page.setFrontmost) m_page.setFrontmost(front);
}

void MobileWebModuleView::setOnDestroyed(std::function<void()> callback)
{
    m_onDestroyed = std::move(callback);
}

void MobileWebModuleView::expireWaits()
{
    if (m_bridge) m_bridge->expireWaits();
}

void MobileWebModuleView::announceDeath()
{
    if (m_announced) return;
    m_announced = true;
    m_alive = false;
    if (m_bridge) m_bridge->close();
    if (m_onDied) m_onDied();
}

} // namespace basecamp::web
