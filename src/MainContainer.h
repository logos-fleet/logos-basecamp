#pragma once

#include "AppNotices.h"
#include "IShellHost.h"

#include <QHash>
#include <QWidget>
#include <QHBoxLayout>
#include <QPointer>
#include <QStackedWidget>

class QQuickWidget;
class WorkspaceArea;
class ShortcutBridge;
class PackageManagerPane;
class AppUnavailablePane;

// MainContainer — the UI shell. Holds exactly one host-side pointer, an
// IShellHost*: no LogosAPI*, no QtLogosCore*, no MainUIBackend*. QML reaches
// the backend as an opaque QObject* via IShellHost::backendObject(). That is
// what lets this class, and everything below it, compile against Qt alone.
class MainContainer : public QWidget, public IShellObserver
{
    Q_OBJECT

public:
    // `host` is borrowed and outlives this widget. Must not be null.
    explicit MainContainer(IShellHost* host, QWidget* parent = nullptr);
    ~MainContainer();

    WorkspaceArea* getWorkspaceArea() const { return m_workspaceArea; }

    // Stops the host from calling back into this object. Idempotent:
    // MainShellView::destroyShell() calls it before deleting, and the
    // destructor calls it again for the path where Qt's parent-child teardown
    // gets here first.
    void detachFromHost();

    // ── IShellObserver ──────────────────────────────────────────────────────
    void onSectionIndexChanged(int index) override;
    void onNavigateToApps() override;
    void onPluginWindowRequested(QWidget* widget, const QString& title) override;
    void onPluginWindowRemoveRequested(QWidget* widget) override;
    // Presentation seam. Branches on the widget pointer because this class
    // decided where each widget was mounted.
    void onPresentAppRequested(QWidget* widget) override;
    // package_manager_ui is answered on its own page; every other refusal is
    // DOCKED, under the app's own name, with the host's reason in the tab
    // (AppNotices.h). A dock that never appeared was the #205 defect's visible
    // half: a tile press that did nothing at all, which the person holding the
    // phone could not tell from a slow load.
    void onUiModuleUnavailable(const QString& name, const QString& reason) override;

protected:
    // Keeps the overlay sized to the full MainContainer: it floats over both
    // the sidebar and the content stack, so it can't sit in the HBoxLayout.
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // Called from QML when the combined visibility of the three overlay
    // dialogs flips. Toggles mouse-event passthrough on the overlay
    // QQuickWidget: transparent when no dialog is open, so the sidebar and
    // content still receive clicks, intercepting when one is.
    void onOverlayActiveChanged(bool active);
    void onSidebarTooltipRequested(const QString& text, qreal y);
    void onAddApplicationDialogRequested(const QVariantMap& metadata);

private:
    // AppNotices' two hands: put an "X cannot open" tab on screen (or rewrite
    // the one already there), and take it away again.
    void raiseNotice(const QString& name, const QString& reason);
    void dropNotice(const QString& name);

    void applyAppManagerSearch(const QString& query);
    void invokeOpenApp(const QString& name, const QString& repositoryUrl);
    void setupUi();

    QHBoxLayout* m_mainLayout;

    QQuickWidget* m_sidebarWidget;

    QStackedWidget* m_contentStack;

    // Workspace (QDockWidget-based, replaces the old QMdiArea workspace)
    WorkspaceArea* m_workspaceArea;

    // QPointer, not a raw pointer: this widget is owned by the HOST side, which
    // deleteLater()s it on unload (UIPluginManager::unloadUiModuleImpl and
    // ::teardownUiPluginWidget both do). As a raw pointer it was read at four
    // sites after the widget had been freed, and `!m_pmuiWidget` stayed false
    // forever, so the Package Manager section could never reload. Same guard the
    // host applies to its own widget maps.
    QPointer<QWidget> m_pmuiWidget;
    // The page that sits in slot 2 while m_pmuiWidget is null. QPointer for the
    // same reason: the stack owns it and it is deleteLater()'d as soon as the
    // real widget takes its place.
    QPointer<PackageManagerPane> m_pmuiPane;
    bool m_suppressNextNavToApps = false;

    // ── "this app cannot come up", on screen (#205) ──────────────────────────
    //
    // The rule is AppNotices'; these are the two hands it drives. The panes are
    // QPointers because the workspace owns each one the moment it is docked and
    // deleteLater()s it with the dock -- the same ownership m_pmuiPane has, and
    // the same reason.
    basecamp::shell::AppNotices m_notices;
    QHash<QString, QPointer<AppUnavailablePane>> m_noticePanes;

    // Content views (QML for Dashboard, Modules, PackageManager, Settings)
    QQuickWidget* m_contentWidget;

    // Full-window overlay for the dependency-aware dialogs
    // (missing-deps popup, cascade-unload/uninstall confirmations).
    // Lives outside the content stack so it's visible regardless of
    // which stack page is current — previously the dialogs were
    // anchored inside m_contentWidget and never rendered while the
    // Apps/MDI screen was active.
    QQuickWidget* m_overlayWidget;

    // Not owned — Window owns it and it outlives this widget.
    IShellHost* m_host;

    ShortcutBridge* m_shortcutBridge = nullptr;
};

