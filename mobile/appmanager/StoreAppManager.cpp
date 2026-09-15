#include "StoreAppManager.h"

namespace basecamp::appmanager {

namespace {

QVariantMap toVariant(const CatalogEntry& e)
{
    return QVariantMap{
        {QStringLiteral("name"), e.name},
        {QStringLiteral("displayName"), e.displayName},
        {QStringLiteral("version"), e.version},
        {QStringLiteral("description"), e.description},
        {QStringLiteral("available"), e.available},
        {QStringLiteral("unavailableReason"), e.unavailableReason},
        {QStringLiteral("variant"), e.variant},
        {QStringLiteral("installed"), e.installed},
        {QStringLiteral("installedVersion"), e.installedVersion},
        // The three the delegate binds its controls to. `canInstall` is the one
        // that makes a native-only row have NO install control rather than a
        // disabled one.
        {QStringLiteral("canInstall"), e.mayOfferInstall()},
        {QStringLiteral("canReport"), e.canReport()},
        {QStringLiteral("canOpenUniversalLink"), e.canOpenUniversalLink()},
    };
}

} // namespace

StoreAppManager::StoreAppManager(Backend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_gate(backend)
{
}

QVariantList StoreAppManager::catalogEntries() const
{
    QVariantList out;
    out.reserve(m_entries.size());
    for (const CatalogEntry& e : m_entries)
        out.append(toVariant(e));
    return out;
}

CatalogEntry StoreAppManager::entry(const QString& name) const
{
    for (const CatalogEntry& e : m_entries)
        if (e.name == name)
            return e;
    return {};
}

QVariantMap StoreAppManager::consentPrompt() const
{
    const ConsentQueue::Prompt p = m_consents.current();
    if (!p.isValid())
        return {};
    return QVariantMap{
        {QStringLiteral("caller"), p.caller},
        {QStringLiteral("target"), p.target},
        {QStringLiteral("callerOrigin"), p.callerOrigin},
        {QStringLiteral("targetOrigin"), p.targetOrigin},
        {QStringLiteral("question"), p.question()},
    };
}

void StoreAppManager::setLastError(const QString& error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
    if (!error.isEmpty())
        emit log(error);
}

void StoreAppManager::refuseWithGateError()
{
    setLastError(m_gate.error());
    // NAME THE DID WHEN THERE IS ONE. Under `require` the common refusal is
    // "signed by a key your keyring does not vouch for", and the DID is the
    // only actionable thing about it -- it is what would be anchored, and a
    // phone has no `lgx keyring` to ask afterwards. `lastError` is the sentence
    // a view shows; this is the line a developer reads off the console to know
    // what to pass to --trust-signer.
    const QVariantMap refused = m_gate.refusedSigner();
    const QString did = refused.value(QStringLiteral("signerDid")).toString();
    if (!did.isEmpty()) {
        const QString name = refused.value(QStringLiteral("name")).toString();
        emit log(QStringLiteral("%1: refused — %2; signer %3 (%4)")
                     .arg(name.isEmpty() ? m_installing : name,
                          m_gate.error(),
                          refused.value(QStringLiteral("signerName")).toString(),
                          did));
    }
    emit signerPromptChanged();
}

void StoreAppManager::refreshCatalog()
{
    m_entries.clear();
    m_catalogUnavailable.clear();

    if (!m_backend || !m_backend->hasCatalog()) {
        // A sentence, not an empty list. A Store shell's Bundled set is data, so
        // a build without the package modules is a legitimate one -- and an empty
        // App Manager reads as "the catalog has nothing in it", which is a
        // different and much more alarming claim than "this build cannot browse".
        m_catalogUnavailable =
            QStringLiteral("this build carries no package modules, so there is no catalog "
                           "to browse");
        emit catalogChanged();
        return;
    }

    // Both, together: an entry's install control depends on the catalog AND on
    // what is installed, and refreshing one without the other offers an Install
    // button for something already on the device.
    const QHash<QString, QString> installed = m_backend->installedVersions();
    const QVariantList rows = m_backend->annotatedCatalog();
    for (const QVariant& row : rows)
        m_entries.append(entryFrom(row.toMap(), installed));

    applyPlatformFloor();

    // A catalog link this shell will not open is worth one line each: it means a
    // repository is publishing something odd, and the alternative is a report
    // affordance that is silently missing.
    for (const CatalogEntry& e : m_entries) {
        if (!e.reportUrlRefusal.isEmpty())
            emit log(QStringLiteral("%1: report link ignored -- %2").arg(e.name, e.reportUrlRefusal));
        if (!e.universalLinkRefusal.isEmpty())
            emit log(QStringLiteral("%1: universal link ignored -- %2")
                         .arg(e.name, e.universalLinkRefusal));
    }

    emit catalogChanged();
}

void StoreAppManager::applyPlatformFloor()
{
    if (!m_floor.isDeclared())
        return;

    // The catalog's OWN dependency graph, assembled from the rows it published.
    // Anything the walk reaches that is not a row here is a name the floor
    // either carries (and then refuses) or has never heard of (and then is not
    // this object's to judge).
    QHash<QString, QStringList> dependencies;
    for (const CatalogEntry& e : m_entries)
        dependencies.insert(e.name, e.dependencies);

    for (CatalogEntry& e : m_entries) {
        const QString missing = m_floor.missingFor(e.name, dependencies);
        if (missing.isEmpty())
            continue;

        // A REFUSAL, not a warning beside an install control. A Bundled set is
        // fixed at build time (ADR 0007), so there is no remedy on the device:
        // an install offered here succeeds and the module dies at its first
        // call, which is the one failure the honest-availability rule exists to
        // prevent. The reason REPLACES package_manager's -- "installable here
        // as the 'web' variant" is no longer true of this row on this shell.
        e.available = false;
        e.variant.clear();
        e.unavailableReason = PlatformFloor::reasonFor(missing);
        emit log(QStringLiteral("%1: %2").arg(e.name, e.unavailableReason));
    }
}

void StoreAppManager::beginInstall(const QString& packageName)
{
    setLastError(QString());
    m_installing = packageName;

    const CatalogEntry e = entry(packageName);
    if (e.name.isEmpty()) {
        // Not in the catalog we are showing. Refusing by name rather than
        // trusting the caller matters because a stale view can outlive a refresh.
        setLastError(QStringLiteral("'%1' is not in this catalog").arg(packageName));
        return;
    }

    if (!m_gate.begin(e)) {
        refuseWithGateError();
        return;
    }
    const QVariantMap prompt = signerPrompt();
    emit log(QStringLiteral("%1: downloaded; signed by %2 (%3)")
                 .arg(packageName,
                      prompt.value(QStringLiteral("signerName")).toString(),
                      prompt.value(QStringLiteral("signerDid")).toString()));
    emit signerPromptChanged();
}

void StoreAppManager::approveSigner()
{
    const QString name = m_installing;
    if (!m_gate.approve()) {
        refuseWithGateError();
        return;
    }
    const QString path = m_gate.installedPath();
    emit signerPromptChanged();
    emit log(QStringLiteral("%1: installed at %2").arg(name, path));
    emit moduleInstalled(name, path);
    // The row's install control has to go, and its installed version has to
    // appear, which is one refresh rather than a patch: the catalog is also how
    // an upgrade offer would appear, and a hand-patched row diverges.
    refreshCatalog();
}

void StoreAppManager::rejectSigner()
{
    m_gate.reject();
    refuseWithGateError();
}

void StoreAppManager::onConsentRequired(const QVariantMap& payload)
{
    if (!m_consents.offer(payload))
        return;
    emit log(QStringLiteral("consent needed: %1").arg(m_consents.current().question()));
    emit consentPromptChanged();
}

void StoreAppManager::answerConsent(bool granted)
{
    const ConsentQueue::Prompt p = m_consents.current();
    if (!p.isValid())
        return;

    const QVariantMap decision = m_consents.answer(granted);
    const bool recorded = m_backend
        && m_backend->decideConsent(decision.value(QStringLiteral("caller")).toString(),
                                    decision.value(QStringLiteral("target")).toString(),
                                    granted);
    if (!recorded) {
        // The user answered and nothing kept the answer. Saying so is the only
        // honest option: the module will ask again, and a silent failure here
        // looks to the user like a decision that did not stick for no reason.
        setLastError(QStringLiteral("could not record your decision about '%1' -> '%2'")
                         .arg(p.caller, p.target));
    } else {
        emit log(QStringLiteral("consent %1: %2 -> %3")
                     .arg(granted ? QStringLiteral("granted") : QStringLiteral("denied"),
                          p.caller, p.target));
    }
    emit consentPromptChanged();
}

void StoreAppManager::dismissConsent()
{
    if (!m_consents.hasPending())
        return;
    m_consents.dismiss();
    emit consentPromptChanged();
}

bool StoreAppManager::openReportLink(const QString& packageName)
{
    const CatalogEntry e = entry(packageName);
    if (!e.canReport()) {
        setLastError(QStringLiteral("'%1' publishes no report link this shell will open")
                         .arg(packageName));
        return false;
    }
    return m_backend && m_backend->openUrl(e.reportUrl);
}

bool StoreAppManager::openUniversalLink(const QString& packageName)
{
    const CatalogEntry e = entry(packageName);
    if (!e.canOpenUniversalLink()) {
        setLastError(QStringLiteral("'%1' publishes no universal link this shell will open")
                         .arg(packageName));
        return false;
    }
    return m_backend && m_backend->openUrl(e.universalLink);
}

} // namespace basecamp::appmanager
