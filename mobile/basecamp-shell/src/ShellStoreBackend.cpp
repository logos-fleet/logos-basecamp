#include "ShellStoreBackend.h"

#include "CoreModuleManager.h"

#include <logos_api.h>
#include <logos_api_client.h>
#include <logos_host_services.h>

#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>

namespace {

const QLatin1String kPackageManager("package_manager");
const QLatin1String kPackageDownloader("package_downloader");
const QLatin1String kCapabilityModule("capability_module");

// A Store shell installs `web` variants AT RUNTIME and nothing else: a phone may
// not download native code (ADR 0003). Its embedded set is native and arrived at
// build time, which is a different list and a different question — see
// package_manager's setInstallableVariants / getValidVariants.
const QLatin1String kRuntimeInstallableVariant("web");

} // namespace

ShellStoreBackend::ShellStoreBackend(LogosAPI* api, CoreModuleManager* modules)
    : m_api(api)
    , m_modules(modules)
{
}

bool ShellStoreBackend::isLoaded(const QString& moduleName) const
{
    return m_modules && m_modules->loadedModules().contains(moduleName);
}

bool ShellStoreBackend::isPresent(const QString& moduleName) const
{
    // What this BUILD carries, which is a different question from what is
    // running: the Native container loads a Bundled member lazily, so a module
    // the app definitely has reads as not-loaded until something asks for it.
    // Measured on a Samsung SM-G990B, 2026-09-13: gating the App Manager on
    // loadedModules() reported "consent unavailable" in a build that carries
    // capability_module, because nothing had called it yet.
    return m_modules && m_modules->knownModules().contains(moduleName);
}

bool ShellStoreBackend::ensureLoaded(const QString& moduleName)
{
    if (isLoaded(moduleName))
        return true;
    if (!isPresent(moduleName))
        return false;
    return m_modules->loadModule(moduleName);
}

QVariant ShellStoreBackend::call(const QString& moduleName, const QString& method,
                                 const QVariantList& args)
{
    if (!m_api)
        return {};
    LogosAPIClient* client = m_api->getClient(moduleName);
    if (!client || !client->isConnected())
        return {};

    // The QVariantList overload, not the per-arity ones: downloadPinned takes
    // four arguments and decideConsent takes four, and a switch over arities is
    // a thing to forget to extend.
    return client->invokeRemoteMethod(moduleName, method, args);
}

bool ShellStoreBackend::configure(const basecamp::appmanager::ModuleDirectories& dirs)
{
    // BOTH, and loaded rather than merely present: browsing needs the downloader
    // and availability needs the manager, and a shell with one of the two would
    // show a catalog every row of which said "this catalog did not say where it
    // runs". Loading here rather than waiting for the first call is what makes
    // hasCatalog() answerable at all -- the container loads lazily, and nothing
    // else in a Store shell ever calls these two.
    const bool manager = ensureLoaded(kPackageManager);
    const bool downloader = ensureLoaded(kPackageDownloader);
    if (!manager || !downloader)
        return false;

    call(kPackageManager, QStringLiteral("setUserModulesDirectory"),
         {dirs.installModulesDir});
    call(kPackageManager, QStringLiteral("setUserUiPluginsDirectory"),
         {dirs.installUiPluginsDir});
    // THE KEYRING, BEFORE THE POLICY. Unset, lgx falls back to a path under
    // $XDG_CONFIG_HOME or $HOME -- an environment variable a phone app does not
    // set and has no claim on -- and `require` then refuses every install while
    // the anchor the user added sits in a directory nothing reads.
    call(kPackageManager, QStringLiteral("setKeyringDirectory"), {dirs.keyringDir});

    // THE STORE SHELL'S POLICY, in two calls.
    //
    // `require` because a Downloaded module on a Store shell is third-party code
    // arriving after review: an unsigned package, or one signed by a publisher
    // this device's keyring does not know, is not installed at all. The desktop
    // default is `warn`.
    call(kPackageManager, QStringLiteral("setSignaturePolicy"), {QStringLiteral("require")});
    // And `web` only. Without this, availability would be answered from the
    // NATIVE variant the loader accepts and every native-only catalog entry
    // would grow an install control that could not work.
    call(kPackageManager, QStringLiteral("setInstallableVariants"),
         {QVariant(QStringList{kRuntimeInstallableVariant})});
    return true;
}

bool ShellStoreBackend::addRepository(const QString& url, QString* error)
{
    if (!isLoaded(kPackageDownloader)) {
        if (error)
            *error = QStringLiteral("package_downloader is not in this build");
        return false;
    }
    const QVariantMap result =
        call(kPackageDownloader, QStringLiteral("addRepository"), {url}).toMap();
    const QString why = result.value(QStringLiteral("error")).toString();
    if (!result.value(QStringLiteral("success"), false).toBool()) {
        // ALREADY THERE IS THE STATE THAT WAS ASKED FOR. The registry is
        // persisted under the app's data directory, so the second launch of the
        // same command hits this -- and reporting it as a failure would say the
        // device is not pointed at a catalog it is in fact pointed at.
        if (why.contains(QStringLiteral("already"), Qt::CaseInsensitive))
            return true;
        if (error)
            *error = why.isEmpty() ? QStringLiteral("addRepository returned no answer") : why;
        return false;
    }
    // AND THE RE-READ, which is not optional and is not the caller's to remember.
    //
    // The library resolves repository metadata ONCE per process and caches every
    // index against it (`ensureMetadata`). The App Manager's first catalog
    // refresh happens at startup, so by the time a repository is added the flag
    // is already set -- and a repository whose metadata was never resolved has
    // an empty `indexUrl`, which reads as "repository metadata declares no
    // indexUrl" and contributes no rows at all. The catalog would come back
    // looking exactly like a catalog that does not carry the package.
    call(kPackageDownloader, QStringLiteral("refreshCatalog"));
    return true;
}

bool ShellStoreBackend::trustSigner(const QString& name, const QString& did, QString* error)
{
    if (!isLoaded(kPackageManager)) {
        if (error)
            *error = QStringLiteral("package_manager is not in this build");
        return false;
    }
    // displayName and url left empty: this is an anchor, and the only fields
    // that decide anything are the name it is filed under and the DID a
    // signature is verified against.
    const QVariantMap result =
        call(kPackageManager, QStringLiteral("addTrustedKey"),
             {name, did, QString(), QString()})
            .toMap();
    if (!result.value(QStringLiteral("success"), false).toBool()) {
        if (error) {
            const QString why = result.value(QStringLiteral("error")).toString();
            *error = why.isEmpty() ? QStringLiteral("addTrustedKey returned no answer") : why;
        }
        return false;
    }
    return true;
}

bool ShellStoreBackend::subscribeToConsent(basecamp::appmanager::StoreAppManager* manager)
{
    if (m_consentSubscribed || !manager || !m_api)
        return m_consentSubscribed;
    // PRESENT, not loaded. onEventWhenAvailable is built to arm against a module
    // that is still coming up, and a Bundled member is loaded lazily -- so
    // requiring it to be running first would refuse to subscribe in precisely
    // the build that needs it.
    if (!isPresent(kCapabilityModule))
        return false;

    LogosAPIClient* client = m_api->getClient(kCapabilityModule);
    if (!client)
        return false;

    // onEventWhenAvailable rather than requestObject + onEvent: the Shell
    // subscribes during its own startup, which is exactly when capability_module
    // is most likely to still be coming up, and the acquiring form would either
    // block this thread or lose the subscription outright.
    QPointer<basecamp::appmanager::StoreAppManager> target(manager);
    const quint64 id = client->onEventWhenAvailable(
        kCapabilityModule, QStringLiteral("consentRequired"),
        [target](const QString&, const QVariantList& args) {
            if (!target || args.isEmpty())
                return;
            // The payload is one JSON string, which is what a universal module's
            // typed event carries.
            const QJsonDocument doc = QJsonDocument::fromJson(args.first().toString().toUtf8());
            if (!doc.isObject())
                return;
            target->onConsentRequired(doc.object().toVariantMap());
        });
    m_consentSubscribed = id != 0;
    return m_consentSubscribed;
}

bool ShellStoreBackend::hasCatalog() const
{
    // What configure() actually managed to bring up. Both, for the reason stated
    // there.
    return isLoaded(kPackageDownloader) && isLoaded(kPackageManager);
}

QVariantList ShellStoreBackend::annotatedCatalog()
{
    const QVariant catalog = call(kPackageDownloader, QStringLiteral("getCatalog"));
    const QVariantList rows = catalog.toList();
    if (rows.isEmpty())
        return {};

    // Annotated in package_manager, not here: the variant vocabulary is
    // logos-package's and the availability rule has exactly one implementation.
    // One call for the whole catalog rather than one per row — a per-row call
    // would be a round trip per entry on the UI thread.
    const QJsonDocument doc = QJsonDocument::fromVariant(rows);
    const QVariant annotated = call(kPackageManager, QStringLiteral("catalogAvailability"),
                                    {QString::fromUtf8(doc.toJson(QJsonDocument::Compact))});
    const QVariantList out = annotated.toList();
    // An unannotated fall-back would read as "available" nowhere — CatalogEntry
    // treats a missing verdict as not installable — so returning the raw rows is
    // safe and keeps the report links visible.
    return out.isEmpty() ? rows : out;
}

QHash<QString, QString> ShellStoreBackend::installedVersions()
{
    QHash<QString, QString> out;
    const QVariantList installed =
        call(kPackageManager, QStringLiteral("getInstalledPackages")).toList();
    for (const QVariant& row : installed) {
        const QVariantMap m = row.toMap();
        const QString name = m.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            out.insert(name, m.value(QStringLiteral("version")).toString());
    }
    return out;
}

QVariantMap ShellStoreBackend::downloadPinned(const QString& repositoryUrl,
                                              const QString& packageName,
                                              const QString& version)
{
    // An empty rootHash means "the newest build of this version", which is what a
    // catalog row is about; pinning a hash is an upgrade-planner's job.
    return call(kPackageDownloader, QStringLiteral("downloadPinned"),
                {repositoryUrl, packageName, version, QString()})
        .toMap();
}

QVariantMap ShellStoreBackend::signerTrust(const QString& lgxPath)
{
    return call(kPackageManager, QStringLiteral("signerTrust"), {lgxPath}).toMap();
}

QVariantMap ShellStoreBackend::installPlugin(const QString& lgxPath)
{
    // skipIfNotNewerVersion=false: the gate above already refused a row that is
    // installed, so anything reaching here is a fresh install and a silent skip
    // would report success for nothing.
    return call(kPackageManager, QStringLiteral("installPlugin"), {lgxPath, false}).toMap();
}

bool ShellStoreBackend::decideConsent(const QString& caller, const QString& target, bool granted)
{
    // THE TRUSTED-CHANNEL ARGUMENT, and the one thing in this file that is not a
    // plain module call.
    //
    // capability_module accepts a consent decision only from the trusted core
    // channel -- otherwise the module under the gate could pass itself through
    // it -- and the credential for that channel is capability_module's OWN
    // token, held by the host. Core authenticates registerRestriction exactly
    // this way (logos-liblogos' module_manager.cpp, registerRestrictionRpc), and
    // a Store shell IS the host: the core runs in this process, so this image's
    // token store is the one the core populated.
    //
    // An empty token here is not a wiring detail to paper over. It means this
    // process is not the trusted channel, the decision will be refused, and the
    // user has to be told their answer went nowhere -- which is what returning
    // false does.
    const std::string trusted = logos::host::tokenFor("capability_module");
    if (trusted.empty())
        return false;

    const QVariant result = call(kCapabilityModule, QStringLiteral("decideConsent"),
                                 {QString::fromStdString(trusted), caller, target, granted});
    return result.toMap().value(QStringLiteral("success"), false).toBool();
}

bool ShellStoreBackend::openUrl(const QUrl& url)
{
    // Already checked by CatalogEntry: https, or http to loopback, and nothing
    // else. This is the handover, and it is the only thing in the App Manager
    // that leaves the app.
    return QDesktopServices::openUrl(url);
}
