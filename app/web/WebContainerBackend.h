#pragma once

#include <web_module_view.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

namespace basecamp::web {

class WebModulePageView;

// BASECAMP'S WEB CONTAINER BACKEND — the one place that says "a page runs here".
//
// liblogos registers the Web container unconditionally and leaves the webview
// unset, so in every process that has no browser a `web` module reports the
// missing bridge instead of "no loader for this format". This installs the
// browser: after install(), loading a `web` module through the ordinary core
// path opens a real page in this process and the container publishes it under
// its own identity, exactly as it publishes a subprocess module.
//
// IT ALSO HOLDS THE VIEWS, because two different owners need them and neither
// can ask the other. The container owns each page's LIFETIME (it destroys the
// view when the module unloads or its page dies) while the shell owns where the
// page GOES ON SCREEN — and the container has no seam through which to hand a
// widget out. So the factory reports here, this announces it, and the shell
// mounts what it is given and drops it when it is told to.
class WebContainerBackend : public QObject {
    Q_OBJECT
public:
    // Process-wide, because the seam it fills is
    // (LogosCore::setWebModuleViewFactory is a process-global). One owner, one
    // registry, and the shell has one object to connect to.
    static WebContainerBackend* instance();

    // Install the factory. `runtimeDir` is where this app keeps its bundled
    // Qt-wasm QML runtime — see logosQmlRuntimeDir(), which is what main()
    // passes. Calling it twice replaces the previous installation.
    //
    // MUST BE CALLED ON THE QT MAIN THREAD, which it then remembers: a page is
    // a widget and a widget may only be built there, while the core may load a
    // module from its own owner thread.
    void install(const QString& runtimeDir);

    // The widget a loaded web module's page draws into, or nullptr. Owned by
    // the view, which is owned by the container — hold it in a QPointer.
    QWidget* widgetFor(const QString& moduleName) const;

    bool hasView(const QString& moduleName) const;

    // Where this app keeps its bundled QML runtime, or an empty string when it
    // ships none.
    //
    //   LOGOS_QML_RUNTIME_DIR      an explicit override, for a developer
    //                              serving a runtime they just built
    //   <app>/../Resources/…       the macOS bundle layout
    //   <app>/../share/…           the Linux install layout
    //   <app>/logos-runtime        beside the executable
    static QString logosQmlRuntimeDir();

signals:
    // A module's page exists and can be mounted. Emitted BEFORE the container
    // asks the page whether it is serving, so the page is on screen while it
    // boots — a 26 MB runtime takes long enough that a shell which waited for
    // the load verdict would show nothing at all for seconds.
    void viewOpened(const QString& moduleName, QWidget* widget);

    // The module's page is going away. The widget is already unusable.
    void viewClosed(const QString& moduleName);

private:
    WebContainerBackend() = default;

    // Called by the factory, always on the Qt main thread.
    WebModulePageView* createView(const LogosCore::WebModuleViewRequest& request,
                                  const QString& runtimeDir);

    QHash<QString, QPointer<WebModulePageView>> m_views;
};

} // namespace basecamp::web
