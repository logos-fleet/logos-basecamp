#pragma once

#include "appmanager/StoreAppManager.h"

#include <QObject>
#include <QString>

class CoreModuleManager;
class LogosAPI;

// THE APP MANAGER'S ONE SEAM, OVER THE REAL MODULES.
//
// basecamp::appmanager::StoreAppManager decides everything about browsing,
// installing, consent and links, and is tested on a desktop with four recorded
// answers. This is the other side of that seam on a phone: the same five
// questions, asked of package_downloader, package_manager, capability_module and
// the operating system.
//
// It is deliberately the only file in the App Manager that includes logos-protocol
// or Qt's platform services, and it holds no policy at all -- every `if` in here
// is about a module not being loaded, which is the one thing this layer knows and
// the layer above cannot.
//
// WHY IT CHECKS WHETHER A MODULE IS LOADED RATHER THAN CALLING AND FAILING. A
// Store shell's Bundled set is data (ADR 0007), so a build that carries neither
// package module is legitimate and common -- the smallest useful shell is chat
// and nothing else. Calling into a module that is not there costs the transport's
// acquire timeout per call, once per catalog refresh, on the UI thread. Asking
// first turns that into one sentence in the App Manager.
class ShellStoreBackend : public basecamp::appmanager::StoreAppManager::Backend {
public:
    // `api` is the Shell's own LogosAPI (the calls authenticate as the Shell, and
    // a module's access policy is written against that name). `modules` answers
    // what the core has actually loaded. Neither is owned.
    ShellStoreBackend(LogosAPI* api, CoreModuleManager* modules);

    // Point package_manager at the directories this shell installs into, and
    // tell it that a Store shell installs `web` variants and nothing else. Must
    // run after package_manager is loaded and before the first catalog refresh:
    // without the variant declaration the module would answer availability from
    // the NATIVE variant the loader accepts, and every native-only catalog entry
    // would grow an install control.
    //
    // Returns false when package_manager is not loaded, which is not an error.
    bool configure(const QString& userModulesDirectory, const QString& userUiPluginsDirectory);

    // Subscribe to capability_module's `consentRequired` and forward each payload
    // to `manager`. Returns false when capability_module is not loaded — in which
    // case no cross-module call is being authorised at all and there is nothing
    // to consent to.
    bool subscribeToConsent(basecamp::appmanager::StoreAppManager* manager);

    // ── StoreAppManager::Backend ──
    bool hasCatalog() const override;
    QVariantList annotatedCatalog() override;
    QHash<QString, QString> installedVersions() override;
    QVariantMap downloadPinned(const QString& repositoryUrl, const QString& packageName,
                               const QString& version) override;
    QVariantMap signerTrust(const QString& lgxPath) override;
    QVariantMap installPlugin(const QString& lgxPath) override;
    bool decideConsent(const QString& caller, const QString& target, bool granted) override;
    bool openUrl(const QUrl& url) override;

private:
    bool isLoaded(const QString& moduleName) const;
    // In this build's Bundled set, whether or not it is running. The Native
    // container loads lazily, so the two answers differ at startup.
    bool isPresent(const QString& moduleName) const;
    // Load it if this build has it and it is not up yet. Nothing else in a Store
    // shell calls the package modules, so if the App Manager does not load them
    // nobody will.
    bool ensureLoaded(const QString& moduleName);
    QVariant call(const QString& moduleName, const QString& method,
                  const QVariantList& args = {});

    LogosAPI*          m_api;      // not owned
    CoreModuleManager* m_modules;  // not owned
    bool               m_consentSubscribed = false;
};
