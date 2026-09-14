#include "ShellModulesBackend.h"

#include "BundledSetCoreRuntime.h"
#include "CoreModuleManager.h"
#include "ShellSections.h"
#include "ShellStoreBackend.h"

#include "appmanager/StoreAppManager.h"
#include "webview/MobileWebContainerBackend.h"

#include <logos_api.h>

#include <QFileInfo>

namespace {

// The caller name this shell introspects modules under. Not "basecamp": the
// desktop app already uses that on the same protocol, and a module's access
// policy is written against the name that calls it.
const char* kApiName = "basecamp_shell";

} // namespace

ShellModulesBackend::ShellModulesBackend(BundledSetCoreRuntime* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_api(new LogosAPI(QString::fromUtf8(kApiName)))
    , m_modules(new CoreModuleManager(m_api, core, this))
    , m_coreModulesModel(new ModuleInstanceModel(this))
    , m_storeBackend(new ShellStoreBackend(m_api, m_modules))
    , m_appManager(new basecamp::appmanager::StoreAppManager(m_storeBackend, this))
{
    // Every line the App Manager produces goes out the one console a Store shell
    // has. It owns no logger, and a refusal nobody can read is a button that did
    // nothing.
    connect(m_appManager, &basecamp::appmanager::StoreAppManager::log,
            this, &ShellModulesBackend::log);

    // A MODULE HAS BEEN INSTALLED, AND THE CORE HAS NOT NOTICED.
    //
    // The App Manager deliberately does not load it (appmanager/README.md):
    // "it did not install" and "it installed and will not run" want different
    // messages. This is the other half, and it is two steps rather than one --
    // discovery is a SCAN of the module directories, so a package written a
    // millisecond ago is not `known` until something rescans. Load only then,
    // and say which of the two failed.
    connect(m_appManager, &basecamp::appmanager::StoreAppManager::moduleInstalled,
            this, &ShellModulesBackend::onModuleInstalled);

    // The same wiring MainUIBackend has: the manager announces "the module set
    // changed" -- on refresh() and on every 2s stats tick -- and the model is
    // rebuilt from it. ModuleInstanceModel patches in place when the names are
    // unchanged, so the tick moves cpu/memory and nothing else.
    connect(m_modules, &CoreModuleManager::coreModulesChanged,
            this, &ShellModulesBackend::rebuildRows);
    rebuildRows();
}

ShellModulesBackend::~ShellModulesBackend()
{
    // m_modules is a QObject child and goes with this object; m_api is not,
    // and is deleted last because the manager's introspection calls reach
    // through it.
    // m_appManager is a QObject child and goes with this object, but it holds a
    // raw pointer to m_storeBackend -- which reaches through m_api -- so the
    // order below is load-bearing: child first, then the backend, then the API.
    delete m_appManager;
    m_appManager = nullptr;
    delete m_storeBackend;
    m_storeBackend = nullptr;
    delete m_modules;
    m_modules = nullptr;
    delete m_api;
}

QObject* ShellModulesBackend::appManagerObject() const
{
    return m_appManager;
}

void ShellModulesBackend::startAppManager(const basecamp::appmanager::ModuleDirectories& dirs)
{
    const bool configured = m_storeBackend->configure(dirs);
    // THE AUTHORITY FIRST, and loaded rather than merely present. See
    // ShellStoreBackend::ensureCapabilityAuthority: every cross-module call on
    // this device is authorised through capability_module, so a Bundled member
    // nobody has called yet is a device on which no module can call another.
    const bool authority = m_storeBackend->ensureCapabilityAuthority();
    // Consent is independent of the catalog: capability_module is in every set
    // (nothing calls anything without it), so a shell with no package modules
    // still prompts for a Downloaded module installed by an earlier launch.
    const bool consent = m_storeBackend->subscribeToConsent(m_appManager);
    emit log(QStringLiteral("app manager: catalog %1, capability authority %2, consent %3")
                 .arg(configured ? QStringLiteral("ready") : QStringLiteral("not in this build"),
                      authority ? QStringLiteral("up") : QStringLiteral("not in this build"),
                      consent ? QStringLiteral("armed") : QStringLiteral("unavailable")));
    // WHERE EVERY MODULE CAME FROM, before anything can call anything. A
    // Downloaded module installed by an earlier launch is already on disk and
    // already discovered, so this launch's FIRST call from it must be gated --
    // which is the whole of "grant persists across restarts".
    declareModuleOrigins();
    m_appManager->refreshCatalog();
}

int ShellModulesBackend::declareModuleOrigins()
{
    const QStringList downloaded = downloadedModules();
    int declared = 0;
    // BOTH HALVES, not just the Downloaded one. capability_module's default is
    // `bundled` and would agree, but a set the app image no longer carries can
    // only be corrected by being told -- and an origin that is merely assumed
    // is one nothing in a console can be checked against.
    for (const QString& name : shippedModuleNames())
        if (m_storeBackend->declareModuleOrigin(name, QStringLiteral("bundled")))
            ++declared;
    for (const QString& name : downloaded)
        if (m_storeBackend->declareModuleOrigin(name, QStringLiteral("downloaded")))
            ++declared;

    emit log(QStringLiteral("app manager: %1 module origin(s) declared to "
                            "capability_module; downloaded: %2")
                 .arg(declared)
                 .arg(downloaded.isEmpty() ? QStringLiteral("none")
                                           : downloaded.join(QStringLiteral(", "))));
    return declared;
}

QVariantMap ShellModulesBackend::consentStatus(const QString& caller,
                                               const QString& target) const
{
    return m_storeBackend->consentStatus(caller, target);
}

bool ShellModulesBackend::ensureRunning(const QString& name)
{
    if (m_modules->loadedModules().contains(name))
        return true;
    if (!m_modules->knownModules().contains(name))
        return false;
    emit log(QStringLiteral("bringing up %1, which this device has and nothing has "
                            "asked for yet").arg(name));
    const bool loaded = m_modules->loadModule(name);
    rebuildRows();
    return loaded;
}

bool ShellModulesBackend::decideConsent(const QString& caller, const QString& target,
                                        bool granted)
{
    const bool recorded = m_storeBackend->decideConsent(caller, target, granted);
    if (!recorded)
        emit log(QStringLiteral("app manager: could not record the decision about "
                                "'%1' -> '%2'").arg(caller, target));
    return recorded;
}

bool ShellModulesBackend::trustSigner(const QString& name, const QString& did)
{
    QString error;
    if (!m_storeBackend->trustSigner(name, did, &error)) {
        emit log(QStringLiteral("app manager: will not trust '%1' (%2): %3")
                     .arg(name, did, error));
        return false;
    }
    // THE DID, IN THE LOG. A keyring entry is what decides whether a signature
    // means anything, and "trusted X" without the string it was anchored under
    // cannot be checked against the prompt that follows.
    emit log(QStringLiteral("app manager: trusting signer '%1' (%2)").arg(name, did));
    return true;
}

void ShellModulesBackend::installFromCatalog(const QString& packageName)
{
    // The gate runs to the signer prompt and stops there; nothing is installed
    // behind it (appmanager/README.md).
    m_appManager->beginInstall(packageName);

    const QVariantMap prompt = m_appManager->signerPrompt();
    if (prompt.isEmpty()) {
        emit log(QStringLiteral("app manager: %1 did not reach the signer prompt: %2")
                     .arg(packageName, m_appManager->lastError()));
        return;
    }
    // WHAT A USER WOULD HAVE READ, before anything is approved. Name and DID
    // together: the name is self-asserted by the publisher, the DID is what the
    // keyring was checked against, and either alone is unverifiable.
    emit log(QStringLiteral("app manager: signer prompt for %1 %2 -- %3 (%4), %5, policy %6, "
                            "installable %7")
                 .arg(prompt.value(QStringLiteral("name")).toString(),
                      prompt.value(QStringLiteral("version")).toString(),
                      prompt.value(QStringLiteral("signerName")).toString(),
                      prompt.value(QStringLiteral("signerDid")).toString(),
                      prompt.value(QStringLiteral("signatureStatus")).toString(),
                      prompt.value(QStringLiteral("policy")).toString(),
                      prompt.value(QStringLiteral("installable")).toBool()
                          ? QStringLiteral("yes") : QStringLiteral("no"))
                 + (prompt.value(QStringLiteral("trusted")).toBool()
                        ? QStringLiteral(", trusted as '%1'")
                              .arg(prompt.value(QStringLiteral("trustedAs")).toString())
                        : QStringLiteral(", NOT in this device's keyring")));

    m_appManager->approveSigner();
    if (!m_appManager->lastError().isEmpty())
        emit log(QStringLiteral("app manager: %1 was not installed: %2")
                     .arg(packageName, m_appManager->lastError()));
}

void ShellModulesBackend::onModuleInstalled(const QString& packageName, const QString& path)
{
    emit log(QStringLiteral("app manager: installed %1 at %2").arg(packageName, path));

    // The scan, then the load. `refreshModules` is the only way a directory
    // written after logos_core_start() becomes discoverable.
    m_core->refreshModules();
    if (!m_modules->knownModules().contains(packageName)) {
        emit log(QStringLiteral("app manager: %1 installed but the core did not "
                                "discover it -- check that %2 is a modules "
                                "directory this build scans")
                     .arg(packageName, QFileInfo(path).absolutePath()));
        return;
    }
    // THE ORIGIN BEFORE THE LOAD, and the order is the whole of it: a `web`
    // module starts calling out while it comes up, and an origin declared after
    // that first call would let it through the 4.7.3 gate.
    declareModuleOrigins();

    if (!m_modules->loadModule(packageName)) {
        emit log(QStringLiteral("app manager: %1 installed and discovered but "
                                "would not load").arg(packageName));
        return;
    }
    emit log(QStringLiteral("app manager: %1 is running").arg(packageName));
    rebuildRows();
}

QAbstractItemModel* ShellModulesBackend::coreModulesModel() const
{
    return m_coreModulesModel;
}

QString ShellModulesBackend::buildVersion() const
{
    return QStringLiteral("mobile");
}

bool ShellModulesBackend::isViewModule(const QString& name) const
{
    for (const QVariant& row : m_core->bundledSet()) {
        const QVariantMap entry = row.toMap();
        if (entry.value(QStringLiteral("name")).toString() == name)
            return entry.value(QStringLiteral("type")).toString()
                       == basecamp::shell::kViewModuleType;
    }
    return false;
}

basecamp::shell::ModuleFacts ShellModulesBackend::facts() const
{
    // bundledSet() is what the app SHIPS -- names, versions and types, including
    // the members the core never registers because a view module is the host's
    // to load (ADR 0006) -- and knownModules()/loadedModules() is what the core
    // has. A row is one answer from each, and a Downloaded module is a name the
    // second has that the first does not.
    basecamp::shell::ModuleFacts out;
    out.bundledSet   = m_core->bundledSet();
    out.known        = m_modules->knownModules();
    out.loaded       = m_modules->loadedModules();
    out.shipped      = m_shipped;
    out.mountedViews = m_mounted;
    out.openPages    = m_openPages;
    return out;
}

QVariantList ShellModulesBackend::snapshot() const
{
    QVariantList rows = basecamp::shell::moduleRows(facts());
    // The only thing the rule above cannot answer: what each module is costing
    // right now. It is a live reading rather than a fact about the set, and it
    // moves every two seconds -- ModuleInstanceModel patches those two roles in
    // place, which is what keeps the table from flickering on every tick.
    for (QVariant& value : rows) {
        QVariantMap row = value.toMap();
        const QVariantMap stats =
            m_modules->moduleStats(row.value(QStringLiteral("name")).toString());
        row[QStringLiteral("cpu")] = stats.value(QStringLiteral("cpu"));
        row[QStringLiteral("memory")] = stats.value(QStringLiteral("memory"));
        // Whether those two are a READING. A Bundled module has no process of
        // its own, so before the Native container learned to measure one it had
        // nothing to report and the row drew 0.0% / 0.0 MB anyway (#86).
        row[QStringLiteral("statsMeasured")] =
            stats.value(QStringLiteral("statsMeasured"), false);
        value = row;
    }
    return rows;
}

void ShellModulesBackend::rebuildRows()
{
    m_coreModulesModel->replaceRows(snapshot());
}

QStringList ShellModulesBackend::bundledSetNames() const
{
    return basecamp::shell::bundledNames(m_core->bundledSet());
}

QStringList ShellModulesBackend::viewModuleNames() const
{
    QStringList names;
    for (const QVariant& row : m_core->bundledSet()) {
        const QVariantMap entry = row.toMap();
        if (entry.value(QStringLiteral("type")).toString()
                == basecamp::shell::kViewModuleType)
            names << entry.value(QStringLiteral("name")).toString();
    }
    return names;
}

QVariantList ShellModulesBackend::launcherApps() const
{
    return basecamp::shell::launcherApps(facts());
}

void ShellModulesBackend::setShippedModules(const QStringList& names)
{
    if (m_shipped == names)
        return;
    m_shipped = names;
    emit launcherAppsChanged();
    rebuildRows();
}

QStringList ShellModulesBackend::shippedModuleNames() const
{
    return bundledSetNames() + m_shipped;
}

QStringList ShellModulesBackend::downloadedModules() const
{
    return basecamp::shell::downloadedModules(facts());
}

bool ShellModulesBackend::isWebContainerApp(const QString& name) const
{
    return basecamp::shell::isWebContainerApp(facts(), name);
}

bool ShellModulesBackend::isHeadlessWebModule(const QString& name) const
{
    using basecamp::web::MobileWebContainerBackend;
    auto* web = MobileWebContainerBackend::instance();
    return web->hasView(name) && !web->pageServesUi(name);
}

void ShellModulesBackend::watchWebContainer()
{
    using basecamp::web::MobileWebContainerBackend;
    auto* web = MobileWebContainerBackend::instance();

    // A PAGE IS WHAT MAKES A DOWNLOADED MODULE AN APP. The Shell holds no
    // manifest for one -- the core discovered it in a directory an install
    // wrote -- so "does this have a UI" is answered by the container having
    // opened a page for it, and by nothing else. A `web` variant of a HEADLESS
    // module opens none and gets no tile.
    connect(web, &MobileWebContainerBackend::viewOpened, this,
            [this, web](const QString& name, void*) {
                if (m_openPages.contains(name))
                    return;
                // A PAGE IS NOT A UI. Every `web` variant gets one -- a wasm
                // image needs a document to live in -- and a `core` module's
                // is a Worker and an empty body. `m_openPages` is what gives a
                // module a sidebar tile and what makes the host mount it, so a
                // headless one must not enter it: the tile would open onto a
                // blank page and the Modules tab would call the module a view.
                if (!web->pageServesUi(name)) {
                    emit log(QStringLiteral("web container: %1 runs in a page and declares "
                                            "no UI -- headless, and no app tile").arg(name));
                    return;
                }
                m_openPages.insert(name);
                emit log(QStringLiteral("web container: %1 has a page").arg(name));
                emit launcherAppsChanged();
                rebuildRows();
            });
    connect(web, &MobileWebContainerBackend::viewClosed, this,
            [this](const QString& name) {
                if (m_openPages.remove(name)) {
                    emit launcherAppsChanged();
                    rebuildRows();
                }
            });

    // What the container ANNOUNCES and deliberately does not do: a module over
    // the live-runtime budget whose package ships no headless document has
    // nowhere to put its page, so the host takes it down through the core --
    // which tears the view down through the ordinary path. A backend that
    // destroyed it itself would leave a published module with a dead channel.
    connect(web, &MobileWebContainerBackend::uiEvictionRequired, this,
            [this](const QString& name) {
                emit log(QStringLiteral("web container: unloading %1, which is over the "
                                        "live-runtime budget and ships no headless "
                                        "document").arg(name));
                m_modules->unloadModuleWithDependents(name);
                rebuildRows();
            });
    // Backgrounded rather than unloaded: the module keeps its channel and keeps
    // answering, and all the Shell has to do is stop claiming its UI is up.
    connect(web, &MobileWebContainerBackend::uiEvicted, this,
            [this](const QString& name) {
                emit log(QStringLiteral("web container: %1 gave up its UI page and is "
                                        "still answering").arg(name));
                if (m_currentVisibleApp == name)
                    setCurrentVisibleApp(QString());
            });

    // Everything the page writes to its own console. A `web` variant draws into
    // a canvas, so what it says is the only thing outside it can read.
    connect(web, &MobileWebContainerBackend::pageLog, this,
            [this](const QString& name, const QString& level, const QString& message) {
                emit log(QStringLiteral("%1 page [%2]: %3").arg(name, level, message));
            });
}

void ShellModulesBackend::setUiModuleMounted(const QString& name, bool mounted)
{
    if (m_mounted.contains(name) == mounted)
        return;
    if (mounted)
        m_mounted.insert(name);
    else
        m_mounted.remove(name);
    emit launcherAppsChanged();
    rebuildRows();
}

void ShellModulesBackend::refreshCoreModules()
{
    m_modules->refresh();
}

void ShellModulesBackend::loadCoreModule(const QString& moduleName)
{
    if (isViewModule(moduleName)) {
        // Not a refusal to be fixed: a view module's image is a Qt-backed
        // framework this process instantiates itself, so the core has no
        // handle on it to load (ADR 0006). It is in the set and in this list,
        // and its state is the host's.
        emit log(QStringLiteral("%1 is a view module: host-loaded, not the core's to load")
                     .arg(moduleName));
        return;
    }
    emit log(QStringLiteral("load %1").arg(moduleName));
    m_modules->loadModule(moduleName);
    rebuildRows();
}

void ShellModulesBackend::unloadCoreModule(const QString& moduleName)
{
    if (isViewModule(moduleName)) {
        emit log(QStringLiteral("%1 is a view module: host-loaded, not the core's to unload")
                     .arg(moduleName));
        return;
    }
    emit log(QStringLiteral("unload %1").arg(moduleName));
    // With dependents, as the desktop's cascade does: a Bundled set is a
    // resolved closure, so taking a member down under a loaded dependent is
    // the one shape of unload that leaves the process pointing at nothing.
    m_modules->unloadModuleWithDependents(moduleName);
    rebuildRows();
}

QString ShellModulesBackend::getCoreModuleMethods(const QString& moduleName)
{
    return m_modules->getMethods(moduleName);
}

QString ShellModulesBackend::getCoreModuleEvents(const QString& moduleName)
{
    return m_modules->getEvents(moduleName);
}

QString ShellModulesBackend::callCoreModuleMethod(const QString& moduleName,
                                                  const QString& methodName,
                                                  const QString& argsJson)
{
    return m_modules->callMethod(moduleName, methodName, argsJson);
}

void ShellModulesBackend::setCurrentActiveSectionIndex(int index)
{
    if (!ShellSection::isValid(index) || index == m_sectionIndex)
        return;
    m_sectionIndex = index;
    emit currentActiveSectionIndexChanged();
}

// ── the apps ────────────────────────────────────────────────────────────────
// All three are one line, and all three are requests rather than actions: the
// widget a mounted app renders into has to reach the Shell's observer, and the
// observer belongs to BundledSetShellHost. This object owns no widgets and is
// the only thing the Shell's QML can see, which is exactly the split
// IShellHost draws on the desktop.

void ShellModulesBackend::loadUiModule(const QString& n)
{
    emit uiModuleLaunchRequested(n);
}

void ShellModulesBackend::unloadUiModule(const QString& n)
{
    emit uiModuleCloseRequested(n);
}

void ShellModulesBackend::onAppLauncherClicked(const QString& n)
{
    // A tile is a toggle on the desktop too: tapping a loaded app brings it to
    // the front rather than loading it twice. mountApp() answers both.
    emit uiModuleLaunchRequested(n);
}

void ShellModulesBackend::setCurrentVisibleApp(const QString& name)
{
    if (m_currentVisibleApp == name)
        return;
    m_currentVisibleApp = name;
    emit currentVisibleAppChanged();
}

// ── inert ───────────────────────────────────────────────────────────────────
// One line each, and each one says what a phone does not have yet rather than
// returning quietly. The Shell renders these surfaces; a user who reaches one
// should get an answer, not a dead control.

namespace {
QString notHere(const char* what)
{
    return QStringLiteral("%1 is not available on this shell yet").arg(QString::fromUtf8(what));
}
} // namespace

void ShellModulesBackend::uninstallApp(const QString& n, const QString&) { emit log(notHere("uninstall") + ": " + n); }
void ShellModulesBackend::confirmUnloadCascade(const QString&)           { emit log(notHere("the unload cascade")); }
void ShellModulesBackend::confirmUninstallCascade(const QString&)        { emit log(notHere("uninstall")); }
void ShellModulesBackend::confirmUninstallMultiCascade(const QStringList&) { emit log(notHere("uninstall")); }
void ShellModulesBackend::cancelMultiUninstall(const QStringList&)       { }
void ShellModulesBackend::cancelPendingAction(const QString&)            { }
void ShellModulesBackend::cancelPendingUninstallApp(const QString&)      { }
void ShellModulesBackend::confirmInstallGate(const QString&)             { emit log(notHere("install")); }
void ShellModulesBackend::cancelInstallGate(const QString&)              { }
void ShellModulesBackend::openApp(const QString& n, const QString&, const QVariantMap&, bool)
{
    emit log(notHere("apps") + ": " + n);
}
void ShellModulesBackend::confirmCatalogInstall(const QString&, const QString&, const QVariantMap&)
{
    emit log(notHere("catalog install"));
}
void ShellModulesBackend::notifyAddApplicationDialogClosed() { }
void ShellModulesBackend::respondToShellIntent(const QString&, bool, const QVariantMap&, const QString&)
{
    emit log(notHere("app-to-app intents"));
}
void ShellModulesBackend::refreshUiModules()   { }
void ShellModulesBackend::refreshRepositories() { emit repositoriesChanged(); }
void ShellModulesBackend::refreshAppCatalog()   { m_appManager->refreshCatalog(); }
void ShellModulesBackend::addRepository(const QString& url)
{
    // THE ONE REPOSITORY CALL A STORE SHELL IMPLEMENTS. The rest of the
    // "Manage Repositories" surface is a desktop affordance this shell does not
    // draw, but pointing the App Manager at a catalog is what makes it an App
    // Manager at all -- and on a phone the only way to do it is from outside
    // (CatalogSource), so a stub here would be a stub in the one path that
    // matters.
    QString error;
    if (!m_storeBackend->addRepository(url, &error)) {
        emit log(QStringLiteral("app manager: cannot add repository %1: %2").arg(url, error));
        return;
    }
    emit log(QStringLiteral("app manager: repository %1 added").arg(url));
    emit repositoriesChanged();
}
void ShellModulesBackend::removeRepository(const QString&)     { emit log(notHere("repositories")); }
void ShellModulesBackend::setRepositoryEnabled(const QString&, bool) { emit log(notHere("repositories")); }
