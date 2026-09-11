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

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class BundledSetCoreRuntime;
class CoreModuleManager;
class LogosAPI;

class ShellModulesBackend : public QObject
{
    Q_OBJECT

    // ── live: the Modules tab ──
    Q_PROPERTY(QAbstractItemModel* coreModulesModel READ coreModulesModel CONSTANT)
    Q_PROPERTY(bool modulesLoading READ modulesLoading NOTIFY modulesLoadingChanged)
    Q_PROPERTY(int currentActiveSectionIndex READ currentActiveSectionIndex
                   WRITE setCurrentActiveSectionIndex NOTIFY currentActiveSectionIndexChanged)

    // ── inert: nothing on a phone answers these yet ──
    Q_PROPERTY(QAbstractItemModel* uiModulesModel READ uiModulesModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel* appsModel READ appsModel CONSTANT)
    Q_PROPERTY(QVariantList requiredPackages READ requiredPackages NOTIFY requiredPackagesChanged)
    Q_PROPERTY(QVariantList launcherApps READ launcherApps NOTIFY launcherAppsChanged)
    Q_PROPERTY(QString currentVisibleApp READ currentVisibleApp NOTIFY currentVisibleAppChanged)
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
    // nullptr, deliberately: an EMPTY model of the wrong shape would answer
    // the Apps Inspector's role names with nothing and look like a working
    // view with no rows. `null` is what the QML already guards for.
    QAbstractItemModel* uiModulesModel() const { return nullptr; }
    QAbstractItemModel* appsModel() const { return nullptr; }

    int currentActiveSectionIndex() const { return m_sectionIndex; }
    bool modulesLoading() const { return false; }
    QVariantList requiredPackages() const { return { }; }
    QVariantList launcherApps() const { return { }; }
    QString currentVisibleApp() const { return { }; }
    QStringList loadingModules() const { return { }; }
    QString buildVersion() const;
    bool isPortableBuild() const { return false; }
    bool isMockBackend() const { return false; }
    QVariantList buildCommits() const { return { }; }
    QVariantList repositories() const { return { }; }
    bool repositoriesLoading() const { return false; }
    bool appsLoading() const { return false; }

    // The Modules tab, as the model holds it right now: one name per row, in
    // row order. The acceptance check reads the SHELL's rows through this
    // rather than re-asking the runtime, which would prove only that the
    // runtime agrees with itself.
    QStringList moduleRowNames() const;

    // The set the app SHIPS, by name, straight off the manifest. The rows
    // above are derived from it, so the two agreeing proves nothing -- what
    // this is for is the view: every shipped module must have a rendered row,
    // and no row may exist that the manifest does not account for.
    QStringList bundledSetNames() const;

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
    void setCurrentActiveSectionIndex(int index);
    void loadCoreModule(const QString& moduleName);
    void unloadCoreModule(const QString& moduleName);
    void loadUiModule(const QString& moduleName);
    void unloadUiModule(const QString& moduleName);
    void onAppLauncherClicked(const QString& appName);
    void setCurrentVisibleApp(const QString& name);

signals:
    void log(const QString& line);

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
    // Whether `name` is a view module: in the Bundled set, but not the core's
    // to load (ADR 0006 -- the host instantiates it and renders its QML).
    bool isHostLoaded(const QString& name) const;

    BundledSetCoreRuntime* m_core;    // not owned
    LogosAPI*              m_api;     // owned
    CoreModuleManager*     m_modules; // owned (parent = this)
    ModuleInstanceModel*   m_coreModulesModel;
    int                    m_sectionIndex = 0;
};
