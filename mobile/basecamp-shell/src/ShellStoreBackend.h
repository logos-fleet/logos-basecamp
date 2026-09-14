#pragma once

#include "appmanager/ModuleDirectories.h"
#include "appmanager/StoreAppManager.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

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

    // Point package_manager at the directories this shell installs into and the
    // keyring it trusts from, and tell it that a Store shell installs `web`
    // variants and nothing else. Must run after package_manager is loaded and
    // before the first catalog refresh: without the variant declaration the
    // module would answer availability from the NATIVE variant the loader
    // accepts, and every native-only catalog entry would grow an install control.
    //
    // Returns false when package_manager is not loaded, which is not an error.
    bool configure(const basecamp::appmanager::ModuleDirectories& dirs);

    // Add a repository (a `logos-repo.json` URL) to package_downloader and
    // re-read the catalog. Already checked by CatalogSource: https, or http to
    // loopback, and nothing else.
    //
    // `error` carries the module's own message. Adding a repository that is
    // already there is SUCCESS: the registry persists across launches, so the
    // second launch of the same command would otherwise report a failure for a
    // device that is in exactly the state that was asked for.
    bool addRepository(const QString& url, QString* error = nullptr);

    // Anchor a publisher in this device's keyring, which is the only thing that
    // makes a signed package installable under the Shell's `require` policy. An
    // explicit act, never derived from what a repository says about itself: a
    // downloaded claim establishes no trust anchor.
    bool trustSigner(const QString& name, const QString& did, QString* error = nullptr);

    // BRING capability_module UP, rather than waiting for something to want it.
    //
    // Every other Bundled member is loaded lazily and that is right: a module
    // nothing has called costs nothing. capability_module is not one of those —
    // it is the authority every cross-module call goes through before it is
    // made, so "nobody has asked for it yet" and "no call can be authorised"
    // are the same state. A module that calls out finds `requestModule` unable
    // to acquire it, gets no token, and the target refuses with "auth token not
    // recognized"; nothing in that chain names the module that is missing.
    //
    // MEASURED, iPad Air 13-inch simulator: the wallet UI's `web` variant asked
    // keystore_module for accounts, `requestModule` failed to acquire
    // capability_module after 0 ms of its 20 s budget, and the call was refused.
    // The Native container had never loaded it because a phone's Shell never
    // calls it itself.
    //
    // Returns false when this build's set has no capability_module, which is a
    // shell where no cross-module call can be authorised at all.
    bool ensureCapabilityAuthority();

    // Subscribe to capability_module's `consentRequired` and forward each payload
    // to `manager`. Returns false when capability_module is not in this build — in
    // which case no cross-module call is being authorised at all and there is
    // nothing to consent to.
    bool subscribeToConsent(basecamp::appmanager::StoreAppManager* manager);

    // TELL capability_module WHERE A MODULE CAME FROM, which is the fact the
    // whole 4.7.3 gate is a function of.
    //
    // It is the HOST's to declare and nobody else's: a module cannot be asked
    // where it came from (it would have every reason to lie), the core does not
    // know (it discovers a Downloaded module in a directory exactly as it
    // discovers a shipped one), and capability_module deliberately does not
    // persist it -- the app image can change under a device between launches, so
    // a remembered origin would outlive the fact. Undeclared, a module is
    // `bundled`, which is the right default for the desktop and for every build
    // that has never installed anything -- and which is also why forgetting to
    // call this does not fail loudly: it silently turns the consent gate off.
    //
    // `origin` is "bundled" or "downloaded". Trusted-channel only, like
    // decideConsent, and for the same reason: a module that could declare
    // itself Bundled would be through the gate.
    bool declareModuleOrigin(const QString& moduleName, const QString& origin);

    // capability_module's own verdict on an ordered pair: { state, reason,
    // callerOrigin, targetOrigin }, where `state` is not-required | unknown |
    // pending | granted | denied. A plain query -- it decides nothing -- and the
    // only way to report why a call was refused in the words of the module that
    // refused it.
    QVariantMap consentStatus(const QString& caller, const QString& target);

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
