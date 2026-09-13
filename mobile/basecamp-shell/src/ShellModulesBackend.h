// The object the Basecamp Shell binds as `backend`, on a phone.
//
// On the desktop that object is MainUIBackend: a facade over CoreModuleManager,
// UIPluginManager and PackageCoordinator. Only the first of those three has an
// answer on a Store shell today -- there is no UI-plugin directory to scan and
// no package_manager module in the Bundled set -- so this is the same QML
// surface with one live quarter and three that say so.
//
// The live quarter is the one this slice is about: `coreModulesModel` is a real
// ModuleInstanceModel (app/ModuleInstanceModel.h, the same class the desktop
// shell's Modules tab binds) filled from CoreModuleManager over ICoreRuntime.
// So the rows, the roles, the sort proxy and the Load/Unload button are the
// desktop's, and what changed underneath is only which ICoreRuntime is
// answering: BundledSetCoreRuntime, over the set the build embedded.
//
// The inert three are stubs and not omissions. QML resolves `backend.foo(...)`
// at call time, so a missing method is a TypeError inside one signal handler
// and the rest of the shell carries on looking fine -- the failure would show
// up as a button that does nothing. A stub that logs is a failure with a name.
#pragma once

#include "ICoreRuntime.h"
#include "ModuleInstanceModel.h"
#include "ShellModuleRows.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class BundledSetCoreRuntime;
class CoreModuleManager;
class LogosAPI;
class ShellStoreBackend;

namespace basecamp::appmanager { class StoreAppManager; }

class ShellModulesBackend : public QObject
{
    Q_OBJECT

    // ── live: the Modules tab ──
    Q_PROPERTY(QAbstractItemModel* coreModulesModel READ coreModulesModel CONSTANT)
    Q_PROPERTY(bool modulesLoading READ modulesLoading NOTIFY modulesLoadingChanged)
    Q_PROPERTY(int currentActiveSectionIndex READ currentActiveSectionIndex
                   WRITE setCurrentActiveSectionIndex NOTIFY currentActiveSectionIndexChanged)

    // ── live: the apps in the Bundled set ──
    // The sidebar's tiles. A Store shell's apps are the `ui_qml` members of
    // the set the build embedded -- there is no UI-plugin directory to scan
    // (ADR 0003) -- so the same QML that lists what UIPluginManager found on
    // disk lists what the manifest says the app carries.
    Q_PROPERTY(QVariantList launcherApps READ launcherApps NOTIFY launcherAppsChanged)
    Q_PROPERTY(QString currentVisibleApp READ currentVisibleApp NOTIFY currentVisibleAppChanged)

    // ── live: the App Manager ──
    // The catalog, the signer-trust prompt and the per-module consent prompt,
    // over the package modules and capability_module. Answered when the Bundled
    // set carries the package modules and says so when it does not -- a Store
    // shell's set is data (ADR 0007) and a build without them is legitimate.
    //
    // A separate object rather than more methods here: everything it decides is
    // platform-free and unit-tested (mobile/appmanager/), and flattening it into
    // this facade would put the rules behind a core.
    Q_PROPERTY(QObject* appManager READ appManagerObject CONSTANT)

    // ── inert: nothing on a phone answers these yet ──
    Q_PROPERTY(QAbstractItemModel* uiModulesModel READ uiModulesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* appsModel READ appsModel CONSTANT)
    Q_PROPERTY(QVariantList requiredPackages READ requiredPackages NOTIFY requiredPackagesChanged)
    Q_PROPERTY(QStringList loadingModules READ loadingModules NOTIFY loadingModulesChanged)
    Q_PROPERTY(QString buildVersion READ buildVersion CONSTANT)
    Q_PROPERTY(bool isPortableBuild READ isPortableBuild CONSTANT)
    Q_PROPERTY(bool isMockBackend READ isMockBackend CONSTANT)
    Q_PROPERTY(QVariantList buildCommits READ buildCommits CONSTANT)
    Q_PROPERTY(QVariantList repositories READ repositories NOTIFY repositoriesChanged)
    Q_PROPERTY(bool repositoriesLoading READ repositoriesLoading NOTIFY repositoriesLoadingChanged)
    Q_PROPERTY(bool appsLoading READ appsLoading NOTIFY appsLoadingChanged)

public:
    // `core` is asked two different questions and must be the same object for
    // both: bundledSet() is what the app SHIPS -- names, versions and types,
    // including the members the core never registers because a view module is
    // the host's to load (ADR 0006) -- and the ICoreRuntime half is what is
    // actually running. A row is one answer from each.
    ShellModulesBackend(BundledSetCoreRuntime* core, QObject* parent = nullptr);
    ~ShellModulesBackend() override;

    QAbstractItemModel* coreModulesModel() const;

    // The App Manager, as QML sees it (QObject* so the property needs no
    // metatype registration for the concrete class).
    QObject* appManagerObject() const;
    basecamp::appmanager::StoreAppManager* appManager() const { return m_appManager; }

    // Point package_manager at this shell's directories, declare the Store
    // shell's policy (`require` signatures, `web` variants only) and subscribe to
    // capability_module's consent announcements. Call once the Bundled set has
    // loaded; a set without those modules is a no-op and the App Manager reports
    // it.
    void startAppManager(const QString& userModulesDirectory,
                         const QString& userUiPluginsDirectory);

    // nullptr, deliberately: an EMPTY model of the wrong shape would answer
    // the Apps Inspector's role names with nothing and look like a working
    // view with no rows. `null` is what the QML already guards for.
    QAbstractItemModel* uiModulesModel() const { return nullptr; }
    QAbstractItemModel* appsModel() const { return nullptr; }

    int currentActiveSectionIndex() const { return m_sectionIndex; }
    bool modulesLoading() const { return false; }
    QVariantList requiredPackages() const { return { }; }
    // One row per `ui_qml` member of the Bundled set, in the row shape
    // UIPluginManager::buildAppRow uses -- the sidebar's delegates read those
    // keys by name, so a shorter map would render a tile with no label.
    QVariantList launcherApps() const;
    QString currentVisibleApp() const { return m_currentVisibleApp; }
    QStringList loadingModules() const { return { }; }
    QString buildVersion() const;
    bool isPortableBuild() const { return false; }
    bool isMockBackend() const { return false; }
    QVariantList buildCommits() const { return { }; }
    QVariantList repositories() const { return { }; }
    bool repositoriesLoading() const { return false; }
    bool appsLoading() const { return false; }

    // The set the app SHIPS, by name, straight off the manifest. The model's
    // rows are derived from it, so reading it back off the model would prove
    // nothing -- what this is for is the view: every shipped module must have
    // a rendered row, and no row may exist that the manifest does not account
    // for. ShellModulesDriver checks that against the scene.
    QStringList bundledSetNames() const;

    // The set's `ui_qml` members, in manifest order. The BUNDLED apps -- a
    // Downloaded one is not here, because it is the core's to load and not the
    // host's to instantiate (see ShellModuleRows.h).
    QStringList viewModuleNames() const;

    // The modules this build SHIPS beside the Bundled-set manifest: the app's
    // own `web-modules` tree, which the core discovers exactly as it discovers
    // an installed package. Told apart by nothing, one of them reads as
    // something the build downloaded for the user.
    //
    // Set once at startup, from the directory the app image carries.
    void setShippedModules(const QStringList& names);
    // Everything the app image carries, under either rule -- what a Store
    // shell's Modules tab is entitled to show with nothing installed.
    QStringList shippedModuleNames() const;

    // Modules the core discovered that neither the manifest nor the shipped
    // tree accounts for: everything a user installed. The Shell's own answer
    // to "where did this come from".
    QStringList downloadedModules() const;
    // Whether this one is a Downloaded module with a page open in the Web
    // container -- i.e. an app the sidebar carries a tile for.
    bool isDownloadedApp(const QString& name) const;

    // Subscribe to the phone's Web container: a Downloaded module's page
    // opening is what makes it an APP here, and there is nothing else the Shell
    // could read that off. Also answers an over-budget module with no headless
    // document by unloading it through the core, which is what the container
    // announces rather than does (webview/MobileWebContainerBackend.h).
    //
    // Separate from the constructor because a build with no container installed
    // is legitimate -- the desktop unit tests construct neither.
    void watchWebContainer();
    // Whether this member is one -- i.e. the host's to instantiate and render
    // rather than the core's to load (ADR 0006).
    bool isViewModule(const QString& name) const;
    // Called by the host once a view module's framework is up and its QML is
    // in a widget, or once it has been taken back down. The ONLY thing that
    // makes an app read as loaded: a `ui_qml` row used to claim it
    // unconditionally, which made "the Modules tab shows what is running" a
    // sentence the tab could not get wrong.
    void setUiModuleMounted(const QString& name, bool mounted);
    // Route a line through the host's console. The host is not a QObject and
    // has no log of its own; everything the Shell says comes out of this one.
    void report(const QString& line) { emit log(line); }

    // ── the Modules tab ──
    Q_INVOKABLE void refreshCoreModules();
    Q_INVOKABLE QString getCoreModuleMethods(const QString& moduleName);
    Q_INVOKABLE QString getCoreModuleEvents(const QString& moduleName);
    Q_INVOKABLE QString callCoreModuleMethod(const QString& moduleName,
                                             const QString& methodName,
                                             const QString& argsJson);
    Q_INVOKABLE QString displayNameFor(const QString& moduleName) const { return moduleName; }

    // ── inert ──
    Q_INVOKABLE void uninstallApp(const QString& name, const QString& repositoryUrl = QString());
    Q_INVOKABLE void confirmUnloadCascade(const QString& moduleName);
    Q_INVOKABLE void confirmUninstallCascade(const QString& moduleName);
    Q_INVOKABLE void confirmUninstallMultiCascade(const QStringList& moduleNames);
    Q_INVOKABLE void cancelMultiUninstall(const QStringList& moduleNames);
    Q_INVOKABLE void cancelPendingAction(const QString& moduleName);
    Q_INVOKABLE void cancelPendingUninstallApp(const QString& name);
    Q_INVOKABLE void confirmInstallGate(const QString& name);
    Q_INVOKABLE void cancelInstallGate(const QString& name);
    Q_INVOKABLE void openApp(const QString& name, const QString& repositoryUrl,
                             const QVariantMap& versionPins = QVariantMap(),
                             bool allowFastLaunch = true);
    Q_INVOKABLE void confirmCatalogInstall(const QString& name, const QString& repositoryUrl,
                                           const QVariantMap& versionPins = QVariantMap());
    Q_INVOKABLE void notifyAddApplicationDialogClosed();
    Q_INVOKABLE void respondToShellIntent(const QString& requestId, bool accepted,
                                          const QVariantMap& payload, const QString& error);
    Q_INVOKABLE void refreshUiModules();
    Q_INVOKABLE void refreshRepositories();
    Q_INVOKABLE void refreshAppCatalog();
    Q_INVOKABLE void addRepository(const QString& url);
    Q_INVOKABLE void removeRepository(const QString& url);
    Q_INVOKABLE void setRepositoryEnabled(const QString& url, bool enabled);

public slots:
    // The other half of StoreAppManager::moduleInstalled: rescan the module
    // directories and bring the new module up. Public rather than private
    // because it is what a driver calls to check the step without installing
    // anything.
    void onModuleInstalled(const QString& packageName, const QString& path);

    void setCurrentActiveSectionIndex(int index);
    void loadCoreModule(const QString& moduleName);
    void unloadCoreModule(const QString& moduleName);
    void loadUiModule(const QString& moduleName);
    void unloadUiModule(const QString& moduleName);
    void onAppLauncherClicked(const QString& appName);
    void setCurrentVisibleApp(const QString& name);

signals:
    void log(const QString& line);

    // The sidebar asked for an app. BundledSetShellHost answers -- it owns the
    // observer the mounted widget has to reach, and the backend owns no
    // widgets. IShellHost::loadUiModule lands on the same two.
    void uiModuleLaunchRequested(const QString& name);
    void uiModuleCloseRequested(const QString& name);

    void currentActiveSectionIndexChanged();
    void modulesLoadingChanged();
    void appsLoadingChanged();
    void currentVisibleAppChanged();
    void launcherAppsChanged();
    void loadingModulesChanged();
    void repositoriesChanged();
    void repositoriesLoadingChanged();
    void requiredPackagesChanged();

private:
    // Rebuilds the rows from the Bundled set + what the core reports. Wired to
    // CoreModuleManager::coreModulesChanged, so a stats tick refreshes cpu and
    // memory in place -- ModuleInstanceModel patches rather than resets when
    // the names are unchanged, which is what keeps the table from flickering
    // every two seconds.
    void rebuildRows();
    QVariantList snapshot() const;
    // The four facts every row and every tile is derived from. One place, so
    // the tab and the sidebar cannot disagree about what is installed.
    basecamp::shell::ModuleFacts facts() const;

    BundledSetCoreRuntime* m_core;    // not owned
    LogosAPI*              m_api;     // owned
    CoreModuleManager*     m_modules; // owned (parent = this)
    ModuleInstanceModel*   m_coreModulesModel;
    // AFTER m_api and m_modules, and that is not cosmetic: members are
    // initialised in DECLARATION order regardless of what the constructor's
    // initialiser list says, and this one is constructed FROM those two. Declared
    // above them it captured two uninitialised pointers and the Shell took a
    // SIGSEGV in CoreModuleManager::loadedModules() on the first catalog probe --
    // measured on a Samsung SM-G990B, 2026-09-13.
    ShellStoreBackend*     m_storeBackend; // owned (plain, not a QObject)
    basecamp::appmanager::StoreAppManager* m_appManager; // owned (parent = this)
    int                    m_sectionIndex = 0;
    // The view modules whose framework is open and whose QML is on screen.
    QSet<QString>          m_mounted;
    // Downloaded modules the Web container has a page open for.
    QSet<QString>          m_openPages;
    // The app's own `web-modules` tree, by name.
    QStringList            m_shipped;
    QString                m_currentVisibleApp;
};
