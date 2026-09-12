#pragma once

#include <web_module_view.h>   // LogosCore::WebModuleView — liblogos' seam

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QWebEngineProfile;
class QWebEngineView;
class QWebChannel;
class QWidget;

namespace basecamp::web {

class LogosWebSchemeHandler;
class PageChannel;

// The object QWebChannel publishes to the page, and the whole of what the page
// can reach in this process.
//
// IT BUFFERS THE HOST-TO-PAGE DIRECTION UNTIL THE PAGE SAYS IT IS LISTENING.
// Not an optimisation: a QWebChannel signal emitted before the JS client has
// finished its handshake reaches nobody — there is no subscriber and no queue
// behind it — while the container starts talking the moment it has published
// the module. The first thing it sends is the wildcard event SUBSCRIBE, so
// without the buffer the page never learns anyone is listening and every event
// it emits goes nowhere, with calls and introspection still working. Same
// finding, same fix, as logoscore-webhost's Bridge.
class PageBridge : public QObject {
    Q_OBJECT
public:
    explicit PageBridge(QObject* parent = nullptr) : QObject(parent) {}

    // Called from the container's side. Always on the Qt main thread — see
    // PageChannel::send, which is what gets it there.
    void deliverToPage(const QString& text);

public slots:
    void toHost(const QString& text) { emit fromPage(text); }
    void closed() { emit pageClosed(); }
    void ready();

signals:
    void toPage(const QString& text);
    void fromPage(const QString& text);
    void pageClosed();

private:
    bool m_pageReady = false;
    QStringList m_backlog;
};

// ONE WEB MODULE'S PAGE, IN THIS PROCESS, AS A WIDGET.
//
// The desktop Web container's view backend. liblogos names no browser on
// purpose (it is linked into a headless CLI, a desktop shell and a static iOS
// core, and exactly one of those can host a Chromium), so the browser lives
// here, in the shell, and this class fills the WebModuleView seam with it.
//
// IN-PROCESS, unlike logoscore's, and that difference is forced rather than
// chosen: basecamp has to PUT THE PAGE ON SCREEN inside its own window, and a
// widget cannot come from another process. What made logoscore spawn a child —
// that WebContainer::awaitLoad and every inbound call block on the thread the
// page delivers on — is already answered in liblogos: both waits PUMP when they
// are on the Qt main thread (WebContainer::awaitLoad, WebModuleGlue::awaitPage),
// exactly as QtRO's blocking call does for a native module. What is genuinely
// lost is crash containment: a renderer that dies is still reported here (the
// page's death is an edge Chromium gives us), but it dies inside basecamp's
// process tree rather than a child the shell outlives.
class WebModulePageView : public QObject, public LogosCore::WebModuleView {
    Q_OBJECT
public:
    // `runtimeDir` is where the app keeps its bundled Qt-wasm QML runtime —
    // the `www` directory of logos-view-module-runtime's `qml-runtime-wasm`.
    // Empty is allowed and means "this app ships no runtime": a `web` variant
    // whose manifest asks for one then fails in the page with a message naming
    // the missing runtime, which beats a blank rectangle.
    WebModulePageView(const LogosCore::WebModuleViewRequest& request,
                      const QString& runtimeDir);
    ~WebModulePageView() override;

    // ── LogosCore::WebModuleView ──────────────────────────────────────────
    logos::web::MessageChannelPtr channel() const override;
    // NO PID, deliberately, and not because one could not be found: Chromium
    // has a renderer process and QWebEnginePage will name it. A web module's
    // pid is what the shell shows as "the module's process", and the renderer
    // is shared machinery the host owns, not the module's own process — the
    // same answer, for the same reason, as InProcContainer::kInProcPid.
    void setOnDied(std::function<void()> callback) override;
    bool isAlive() const override;

    // The widget the shell mounts. Owned by this view, so it is gone when the
    // container unloads the module — hold it in a QPointer, never a raw member.
    QWidget* widget() const;

    const QString& moduleName() const { return m_moduleName; }

    // The page could not be brought up at all (no runtime, no entry document,
    // no qwebchannel.js in this Qt build). The container's own verdict is still
    // "the page published no module", which is the same outcome; this is what
    // says WHY in the log.
    const QString& startupError() const { return m_startupError; }

private:
    void announceDeath();

    QString m_moduleName;
    QString m_startupError;
    QPointer<QWebEngineProfile> m_profile;
    QPointer<QWebEngineView> m_view;
    QPointer<PageBridge> m_bridge;
    QPointer<QWebChannel> m_webChannel;
    LogosWebSchemeHandler* m_handler = nullptr;   // owned by m_profile
    std::shared_ptr<PageChannel> m_channel;
    std::function<void()> m_onDied;
    bool m_announced = false;
    bool m_alive = true;
};

} // namespace basecamp::web
