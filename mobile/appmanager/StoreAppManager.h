#pragma once

#include "CatalogEntry.h"
#include "ConsentQueue.h"
#include "InstallGate.h"
#include "PlatformFloor.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace basecamp::appmanager {

// THE APP MANAGER ON A STORE SHELL, AS QML SEES IT.
//
// Everything a catalog entry means (CatalogEntry), the order an install happens
// in (InstallGate) and the shape of the consent queue (ConsentQueue) are decided
// elsewhere and tested there. This is the object that owns the three, holds the
// current state, and turns it into properties a view can bind to.
//
// ONE SEAM, `Backend`, for everything outside this process: the two package
// modules, capability_module, and the platform's "open this URL". A Store shell
// implements it over LogosAPI; the tests implement it with four recorded
// answers. That is the difference between an App Manager whose rules are tested
// on a desktop in half a second and one that can only be tried on a phone.
//
// WHAT IT DOES WHEN THERE IS NO CATALOG. A Store shell's Bundled set is data
// (ADR 0007) and a build is free not to include the package modules — the
// smallest useful shell is chat and nothing else. So "the catalog is not in this
// build" is a normal state, not an error, and it is reported as a sentence
// rather than as an empty list: an empty App Manager looks like a catalog with
// nothing in it, which is a different and much more alarming thing.
class StoreAppManager : public QObject {
    Q_OBJECT

    // The catalog, annotated. Each element is a CatalogEntry flattened for QML:
    // name, displayName, version, description, available, unavailableReason,
    // variant, installed, installedVersion, canInstall, canReport,
    // canOpenUniversalLink.
    Q_PROPERTY(QVariantList catalogEntries READ catalogEntries NOTIFY catalogChanged)
    // Empty while the catalog is usable; otherwise why it is not.
    Q_PROPERTY(QString catalogUnavailableReason READ catalogUnavailableReason NOTIFY catalogChanged)

    // The signer-trust prompt. Empty unless one is on screen.
    Q_PROPERTY(QVariantMap signerPrompt READ signerPrompt NOTIFY signerPromptChanged)
    // WHO the last refusal refused, when it had a signer to name. Empty
    // otherwise. Never a prompt: this is identity without an Install button,
    // for the case the Store shell's `require` policy exists to produce (ADR
    // 0008), where the DID is the only way forward and `lastError` is a
    // sentence that cannot carry it. Changes with `signerPrompt`, which is why
    // it shares the signal -- the two are the two halves of one verdict.
    Q_PROPERTY(QVariantMap refusedSigner READ refusedSigner NOTIFY signerPromptChanged)
    // The per-module consent prompt (guideline 4.7.3). Empty unless one is on
    // screen; otherwise { caller, target, callerOrigin, targetOrigin, question }.
    Q_PROPERTY(QVariantMap consentPrompt READ consentPrompt NOTIFY consentPromptChanged)
    Q_PROPERTY(int pendingConsentCount READ pendingConsentCount NOTIFY consentPromptChanged)

    // The last thing that went wrong, for the one place a view can show it.
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    class Backend : public InstallGate::Modules {
    public:
        // Whether this build has the package modules at all. False is a normal
        // state for a Store shell whose Bundled set does not include them.
        virtual bool hasCatalog() const = 0;

        // package_downloader.getCatalog, run through
        // package_manager.catalogAvailability. Annotating in the MODULE rather
        // than here is deliberate: the variant vocabulary is logos-package's and
        // the availability rule has exactly one implementation.
        virtual QVariantList annotatedCatalog() = 0;

        // package_manager.getInstalledPackages, projected to name -> version.
        virtual QHash<QString, QString> installedVersions() = 0;

        // capability_module.decideConsent. Returns whether it was recorded —
        // a refusal here means the Shell asked without the trusted channel, and
        // the user's answer went nowhere, which they must be told.
        virtual bool decideConsent(const QString& caller, const QString& target,
                                   bool granted) = 0;

        // Hand a URL to the operating system. Already checked by CatalogEntry:
        // https, or http to loopback, and nothing else.
        virtual bool openUrl(const QUrl& url) = 0;
    };

    explicit StoreAppManager(Backend* backend, QObject* parent = nullptr);

    // THIS BUILD'S PLATFORM FLOOR (#169), derived by the build and read back out
    // of the Bundled-set manifest by the host that constructs this object.
    //
    // It is set rather than asked for through `Backend` because it is not a
    // question about anything outside the process: the answer was computed at
    // build time, compiled into the app image, and is the same for the life of
    // the app. A shell that never calls this declares no floor, which refuses
    // nothing -- see PlatformFloor.
    void setPlatformFloor(const PlatformFloor& floor) { m_floor = floor; }

    QVariantList catalogEntries() const;
    QString catalogUnavailableReason() const { return m_catalogUnavailable; }
    QVariantMap signerPrompt() const { return m_gate.signerPrompt(); }
    QVariantMap refusedSigner() const { return m_gate.refusedSigner(); }
    QVariantMap consentPrompt() const;
    int pendingConsentCount() const { return m_consents.pendingCount(); }
    QString lastError() const { return m_lastError; }

    // The entry by name, for a host that wants the decisions rather than the
    // flattened map.
    CatalogEntry entry(const QString& name) const;

public slots:
    // Re-read the catalog and what is installed. The two go together: an entry's
    // install control depends on both, and refreshing one without the other shows
    // an Install button for something that is already there.
    void refreshCatalog();

    // Start an install. Runs the whole gate up to the signer prompt; the prompt
    // is then on screen and nothing else happens until approveSigner /
    // rejectSigner.
    void beginInstall(const QString& packageName);
    void approveSigner();
    void rejectSigner();

    // A `consentRequired` payload from capability_module.
    void onConsentRequired(const QVariantMap& payload);
    // Record the user's answer to the prompt on screen.
    void answerConsent(bool granted);
    // "Not now": drop the prompt, record nothing, and let it be asked again.
    void dismissConsent();

    // Open one of a row's two links. Returns false when the row has none this
    // shell will open — which is not the same as "the OS refused".
    bool openReportLink(const QString& packageName);
    bool openUniversalLink(const QString& packageName);

signals:
    void catalogChanged();
    void signerPromptChanged();
    void consentPromptChanged();
    void lastErrorChanged();

    // Every line the host puts in its console. The App Manager has no logger of
    // its own, and a Store shell's whole diagnostic surface is that console.
    void log(const QString& line);

    // A module was installed. The Shell loads it; this object does not, because
    // "it did not install" and "it installed and will not run" want different
    // messages and different remedies.
    void moduleInstalled(const QString& packageName, const QString& path);

private:
    void setLastError(const QString& error);
    void refuseWithGateError();
    // Apply the Platform floor to `m_entries`, in place. Runs after the rows are
    // built because the walk is over the CATALOG's dependency graph and a row's
    // verdict can therefore depend on a row further down the list.
    void applyPlatformFloor();

    Backend*      m_backend;  // not owned
    InstallGate   m_gate;
    PlatformFloor m_floor;
    ConsentQueue  m_consents;

    QList<CatalogEntry> m_entries;
    QString             m_catalogUnavailable;
    QString             m_lastError;
    QString             m_installing;
};

} // namespace basecamp::appmanager
