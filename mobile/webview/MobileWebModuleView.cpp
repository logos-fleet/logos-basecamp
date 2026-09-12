#include "webview/MobileWebModuleView.h"

#include <QDebug>
#include <QDir>

#include <utility>

namespace basecamp::web {

namespace {

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

    m_bridge = std::make_shared<MobileWebBridge>(moduleDir, runtimeDir, entryFile,
                                                std::move(origin));
    m_bridge->setInjectsShimIntoHtml(shimInDocument);
    m_channel = std::make_shared<BridgeChannel>(m_bridge);

    // A page closing its channel is a module reporting that it has stopped
    // serving — a trapped wasm image does exactly this. Same verdict as a dead
    // webview, because it is the same fact.
    m_bridge->setOnPageClosed([this] {
        qWarning() << "Web module" << m_moduleName
                   << "closed its channel; it is no longer serving";
        announceDeath();
    });

    PlatformPageRequest pageRequest;
    pageRequest.moduleName = m_moduleName;
    pageRequest.entryUrl = m_bridge->entryUrl();
    // EMPTY when the document carries it: a platform that injects AND a
    // container that serves it inside the HTML would run the shim twice, and
    // the second one would take over the channel the first had already opened.
    pageRequest.channelShim = shimInDocument ? QString() : m_bridge->channelShim();
    auto bridge = m_bridge;
    pageRequest.serve = [bridge](const QByteArray& method, const QUrl& url,
                                 const QByteArray& body, MobileWebBridge::Respond respond) {
        bridge->handleRequest(method, url, body, std::move(respond));
    };
    pageRequest.onDied = [this] {
        qWarning() << "Web module" << m_moduleName << "lost its webview";
        announceDeath();
    };

    m_page = platform(pageRequest);
    if (!m_page.destroy) {
        m_startupError = QStringLiteral("the platform could not open a webview");
        qWarning() << "Web module" << m_moduleName << ":" << m_startupError;
        m_bridge->close();
    }
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
