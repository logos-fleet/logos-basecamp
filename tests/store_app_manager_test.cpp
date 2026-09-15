// srcdeps: appmanager/StoreAppManager.cpp appmanager/InstallGate.cpp appmanager/CatalogEntry.cpp appmanager/ConsentQueue.cpp appmanager/PlatformFloor.cpp
//
// THE APP MANAGER ON A STORE SHELL (slice 29).
//
// What a catalog entry means, the order an install happens in and the shape of
// the consent queue are each tested on their own (catalog_entry_test,
// install_gate_signer_test, consent_queue_test). This is the object that owns the
// three, and what is left to test is the WIRING: which calls a user action turns
// into, in what order, and what the object says when one of them does not answer.
//
// Everything outside the process is one seam, so all of that runs on a desktop in
// under a second. The parts that genuinely need a phone -- a real catalog release
// over a real network, a real `web` variant landing in the Web container -- are
// what is left over once these pass.
//
// Run: nix build .#unit-tests -L

#include "appmanager/StoreAppManager.h"

#include <QtTest/QtTest>

using basecamp::appmanager::PlatformFloor;
using basecamp::appmanager::StoreAppManager;

namespace {

// Everything outside this process, recorded.
class FakeBackend : public StoreAppManager::Backend {
public:
    QStringList calls;

    bool catalogPresent = true;
    QVariantList catalog;
    QHash<QString, QString> installed;
    bool consentRecordable = true;
    bool urlOpenable = true;
    QList<QUrl> opened;

    QVariantMap downloadAnswer{
        {QStringLiteral("success"), true},
        {QStringLiteral("path"), QStringLiteral("/tmp/counter_ui.lgx")},
    };
    QVariantMap signerAnswer{
        {QStringLiteral("name"), QStringLiteral("counter_ui")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("signatureStatus"), QStringLiteral("signed")},
        {QStringLiteral("signerName"), QStringLiteral("Acme Modules")},
        {QStringLiteral("signerDid"), QStringLiteral("did:jwk:acme")},
        {QStringLiteral("trusted"), true},
        {QStringLiteral("installable"), true},
    };
    QVariantMap installAnswer{
        {QStringLiteral("path"), QStringLiteral("/data/modules/counter_ui/index.js")},
    };

    bool hasCatalog() const override { return catalogPresent; }

    QVariantList annotatedCatalog() override
    {
        calls << QStringLiteral("annotatedCatalog");
        return catalog;
    }

    QHash<QString, QString> installedVersions() override
    {
        calls << QStringLiteral("installedVersions");
        return installed;
    }

    QVariantMap downloadPinned(const QString&, const QString& packageName,
                               const QString& version) override
    {
        calls << QStringLiteral("download:%1@%2").arg(packageName, version);
        return downloadAnswer;
    }

    QVariantMap signerTrust(const QString& path) override
    {
        calls << QStringLiteral("signerTrust:%1").arg(path);
        return signerAnswer;
    }

    QVariantMap installPlugin(const QString& path) override
    {
        calls << QStringLiteral("install:%1").arg(path);
        return installAnswer;
    }

    bool decideConsent(const QString& caller, const QString& target, bool granted) override
    {
        calls << QStringLiteral("decideConsent:%1->%2=%3")
                     .arg(caller, target, granted ? QStringLiteral("yes") : QStringLiteral("no"));
        return consentRecordable;
    }

    bool openUrl(const QUrl& url) override
    {
        calls << QStringLiteral("openUrl:%1").arg(url.toString());
        opened << url;
        return urlOpenable;
    }
};

QVariantMap catalogRow(const QString& name, bool available, const QString& reason,
                       const QString& reportUrl = {}, const QString& universalLink = {})
{
    QVariantMap manifest{{QStringLiteral("version"), QStringLiteral("1.2.0")}};
    QVariantMap version{{QStringLiteral("manifest"), manifest}};
    QVariantMap availability{
        {QStringLiteral("available"), available},
        {QStringLiteral("variant"), available ? QStringLiteral("web") : QString()},
        {QStringLiteral("reason"), reason},
    };
    QVariantMap row{
        {QStringLiteral("name"), name},
        {QStringLiteral("repositoryUrl"), QStringLiteral("https://logos.test/logos-repo.json")},
        {QStringLiteral("versions"), QVariantList{version}},
        {QStringLiteral("availability"), availability},
    };
    if (!reportUrl.isEmpty())
        row[QStringLiteral("reportUrl")] = reportUrl;
    if (!universalLink.isEmpty())
        row[QStringLiteral("universalLink")] = universalLink;
    return row;
}

QVariantMap webRow()
{
    return catalogRow(QStringLiteral("counter_ui"), true,
                      QStringLiteral("installable here as the 'web' variant"),
                      QStringLiteral("https://logos.test/report?m=counter_ui"),
                      QStringLiteral("https://logos.test/m/counter_ui"));
}

// package_manager's two refusing verdicts: nothing signed the package at all,
// and signed by a publisher no anchor in this device's keyring validates. The
// second is the case a Store shell's `require` policy exists to produce.
QVariantMap unsignedVerdict()
{
    return QVariantMap{
        {QStringLiteral("signatureStatus"), QStringLiteral("unsigned")},
        {QStringLiteral("installable"), false},
        {QStringLiteral("reason"),
         QStringLiteral("this package is unsigned and this build requires a signature")},
    };
}

QVariantMap unanchoredSignerVerdict()
{
    return QVariantMap{
        {QStringLiteral("name"), QStringLiteral("counter_ui")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("signatureStatus"), QStringLiteral("signed")},
        {QStringLiteral("signerName"), QStringLiteral("Acme Modules")},
        {QStringLiteral("signerDid"), QStringLiteral("did:jwk:acme")},
        {QStringLiteral("trusted"), false},
        {QStringLiteral("trustedAs"), QString()},
        {QStringLiteral("policy"), QStringLiteral("require")},
        {QStringLiteral("installable"), false},
        {QStringLiteral("reason"),
         QStringLiteral("signed by a key your keyring does not vouch for")},
    };
}

// A row whose manifest declares dependencies. The catalog publishes a package's
// dependency list as the package itself states it (logos-package-downloader
// carries the whole manifest through), which is what makes the Platform-floor
// walk a question about data rather than about a table kept here.
QVariantMap dependentRow(const QString& name, const QStringList& dependencies)
{
    QVariantMap row = catalogRow(name, true,
                                 QStringLiteral("installable here as the 'web' variant"));
    QVariantMap manifest{{QStringLiteral("version"), QStringLiteral("1.2.0")},
                         {QStringLiteral("dependencies"), QVariant(dependencies)}};
    row[QStringLiteral("versions")] =
        QVariantList{QVariantMap{{QStringLiteral("manifest"), manifest}}};
    return row;
}

QVariantMap nativeOnlyRow()
{
    return catalogRow(QStringLiteral("desktop_only"), false,
                      QStringLiteral("available on macOS and Linux, not in this build"),
                      QStringLiteral("https://logos.test/report?m=desktop_only"),
                      QStringLiteral("https://logos.test/m/desktop_only"));
}

QVariantMap rowByName(const StoreAppManager& m, const QString& name)
{
    for (const QVariant& v : m.catalogEntries())
        if (v.toMap().value(QStringLiteral("name")).toString() == name)
            return v.toMap();
    return {};
}

QVariantMap consentRequired(const QString& caller, const QString& target)
{
    return QVariantMap{
        {QStringLiteral("caller"), caller},
        {QStringLiteral("target"), target},
        {QStringLiteral("callerOrigin"), QStringLiteral("downloaded")},
        {QStringLiteral("targetOrigin"), QStringLiteral("bundled")},
    };
}

} // namespace

class StoreAppManagerTest : public QObject {
    Q_OBJECT

private slots:
    // ── browsing ────────────────────────────────────────────────────────────

    void refreshReadsTheCatalogAndWhatIsInstalled()
    {
        // Both, together. An entry's install control depends on each, and
        // refreshing one without the other offers an Install button for
        // something already on the device.
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);

        m.refreshCatalog();

        QVERIFY(backend.calls.contains(QStringLiteral("annotatedCatalog")));
        QVERIFY(backend.calls.contains(QStringLiteral("installedVersions")));
        QCOMPARE(m.catalogEntries().size(), 1);
        QVERIFY(m.catalogUnavailableReason().isEmpty());
    }

    void aNativeOnlyRowHasNoInstallControlAndCarriesTheReason()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow(), nativeOnlyRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        const QVariantMap web = rowByName(m, QStringLiteral("counter_ui"));
        QVERIFY(web.value(QStringLiteral("canInstall")).toBool());
        QCOMPARE(web.value(QStringLiteral("variant")).toString(), QStringLiteral("web"));

        const QVariantMap native = rowByName(m, QStringLiteral("desktop_only"));
        QVERIFY(!native.value(QStringLiteral("canInstall")).toBool());
        QCOMPARE(native.value(QStringLiteral("unavailableReason")).toString(),
                 QStringLiteral("available on macOS and Linux, not in this build"));
    }

    // ── the Platform floor (#169) ───────────────────────────────────────────

    void aRowThatNeedsAPlatformModuleThisBuildLacksIsNotOffered()
    {
        // ADR 0009's second rule, at the only moment it can still be honoured:
        // a Downloaded module may depend on a Platform module only where that
        // module is in the Bundled set of the shell it lands on. A Bundled set
        // is fixed at build time, so "install it and find out" has no remedy --
        // the module installs and dies at its first call.
        FakeBackend backend;
        backend.catalog = QVariantList{
            dependentRow(QStringLiteral("chat_module"), {QStringLiteral("delivery_module")})};
        StoreAppManager m(&backend);
        m.setPlatformFloor(PlatformFloor({}, {QStringLiteral("delivery_module")}));

        m.refreshCatalog();

        const QVariantMap row = rowByName(m, QStringLiteral("chat_module"));
        QVERIFY(!row.value(QStringLiteral("available")).toBool());
        QVERIFY(!row.value(QStringLiteral("canInstall")).toBool());
        QCOMPARE(row.value(QStringLiteral("unavailableReason")).toString(),
                 QStringLiteral("requires delivery_module, not in this build"));
        // Nothing to name: there is no variant an install here would use.
        QVERIFY(row.value(QStringLiteral("variant")).toString().isEmpty());
    }

    void thePlatformFloorIsWalkedThroughTheCatalogsOwnRows()
    {
        // THE SHARP ONE. chat_ui declares chat_module and nothing else, and
        // chat_module is not itself a Platform module -- it is built on one. The
        // missing module is two edges down, and the row must still name IT
        // rather than the intermediate the user has never heard of.
        FakeBackend backend;
        backend.catalog = QVariantList{
            dependentRow(QStringLiteral("chat_ui"), {QStringLiteral("chat_module")}),
            dependentRow(QStringLiteral("chat_module"), {QStringLiteral("delivery_module")})};
        StoreAppManager m(&backend);
        m.setPlatformFloor(PlatformFloor({}, {QStringLiteral("delivery_module")}));

        m.refreshCatalog();

        QCOMPARE(rowByName(m, QStringLiteral("chat_ui"))
                     .value(QStringLiteral("unavailableReason")).toString(),
                 QStringLiteral("requires delivery_module, not in this build"));
    }

    void aShellThatShipsThePlatformModuleStillOffersTheRow()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{
            dependentRow(QStringLiteral("chat_ui"), {QStringLiteral("chat_module")}),
            dependentRow(QStringLiteral("chat_module"), {QStringLiteral("delivery_module")})};
        StoreAppManager m(&backend);
        m.setPlatformFloor(PlatformFloor({QStringLiteral("delivery_module")}, {}));

        m.refreshCatalog();

        QVERIFY(rowByName(m, QStringLiteral("chat_ui"))
                    .value(QStringLiteral("canInstall")).toBool());
    }

    void aBuildThatDeclaredNoFloorLeavesEveryVerdictAlone()
    {
        // An app image whose manifest predates the derivation. Refusing the
        // catalog on that basis would take a working shell's App Manager away,
        // which is a far worse failure than the one the floor prevents.
        FakeBackend backend;
        backend.catalog = QVariantList{
            dependentRow(QStringLiteral("chat_module"), {QStringLiteral("delivery_module")})};
        StoreAppManager m(&backend);

        m.refreshCatalog();

        QVERIFY(rowByName(m, QStringLiteral("chat_module"))
                    .value(QStringLiteral("canInstall")).toBool());
    }

    void anInstalledRowReportsItsVersionAndOffersNoInstall()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        backend.installed.insert(QStringLiteral("counter_ui"), QStringLiteral("1.0.0"));
        StoreAppManager m(&backend);
        m.refreshCatalog();

        const QVariantMap row = rowByName(m, QStringLiteral("counter_ui"));
        QVERIFY(row.value(QStringLiteral("installed")).toBool());
        QCOMPARE(row.value(QStringLiteral("installedVersion")).toString(),
                 QStringLiteral("1.0.0"));
        QVERIFY(!row.value(QStringLiteral("canInstall")).toBool());
    }

    void aShellWithNoPackageModulesSaysSoRatherThanShowingAnEmptyCatalog()
    {
        // A Store shell's Bundled set is data, so a build without the package
        // modules is legitimate -- and an empty App Manager reads as "the catalog
        // has nothing in it", which is a different and much more alarming claim.
        FakeBackend backend;
        backend.catalogPresent = false;
        StoreAppManager m(&backend);

        m.refreshCatalog();

        QVERIFY(m.catalogEntries().isEmpty());
        QVERIFY(!m.catalogUnavailableReason().isEmpty());
        QVERIFY(m.catalogUnavailableReason().contains(QStringLiteral("no package modules")));
        // And it asked nothing.
        QVERIFY(backend.calls.isEmpty());
    }

    void aCatalogLinkThisShellWillNotOpenIsLogged()
    {
        // The alternative is a report affordance that is silently missing, which
        // looks like a catalog that publishes none.
        FakeBackend backend;
        backend.catalog = QVariantList{catalogRow(
            QStringLiteral("evil"), true, QStringLiteral("ok"),
            QStringLiteral("javascript:alert(1)"), QStringLiteral("http://plain.test/m/evil"))};
        StoreAppManager m(&backend);

        QStringList lines;
        connect(&m, &StoreAppManager::log, [&lines](const QString& l) { lines << l; });
        m.refreshCatalog();

        QCOMPARE(lines.size(), 2);
        QVERIFY(lines.at(0).contains(QStringLiteral("report link ignored")));
        QVERIFY(lines.at(1).contains(QStringLiteral("universal link ignored")));
        QVERIFY(!rowByName(m, QStringLiteral("evil")).value(QStringLiteral("canReport")).toBool());
    }

    // ── installing ──────────────────────────────────────────────────────────

    void installingStopsAtTheSignerPromptAndNamesTheSigner()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        backend.calls.clear();

        m.beginInstall(QStringLiteral("counter_ui"));

        QCOMPARE(backend.calls.size(), 2);
        QVERIFY(backend.calls.at(0).startsWith(QStringLiteral("download:counter_ui@1.2.0")));
        QVERIFY(backend.calls.at(1).startsWith(QStringLiteral("signerTrust:")));
        QCOMPARE(m.signerPrompt().value(QStringLiteral("signerName")).toString(),
                 QStringLiteral("Acme Modules"));
        QCOMPARE(m.signerPrompt().value(QStringLiteral("signerDid")).toString(),
                 QStringLiteral("did:jwk:acme"));
    }

    void approvingInstallsAndAnnouncesAndRefreshes()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        m.beginInstall(QStringLiteral("counter_ui"));

        QSignalSpy installedSpy(&m, &StoreAppManager::moduleInstalled);
        backend.calls.clear();

        m.approveSigner();

        QVERIFY(backend.calls.contains(QStringLiteral("install:/tmp/counter_ui.lgx")));
        QCOMPARE(installedSpy.count(), 1);
        QCOMPARE(installedSpy.at(0).at(0).toString(), QStringLiteral("counter_ui"));
        QCOMPARE(installedSpy.at(0).at(1).toString(),
                 QStringLiteral("/data/modules/counter_ui/index.js"));
        // And the row was re-read, so its install control can go.
        QVERIFY(backend.calls.contains(QStringLiteral("annotatedCatalog")));
        QVERIFY(m.signerPrompt().isEmpty());
    }

    void theShellDoesNotLoadTheModuleItself()
    {
        // Loading is the Shell's, not this object's: "it did not install" and
        // "it installed and will not run" want different messages.
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        m.beginInstall(QStringLiteral("counter_ui"));
        m.approveSigner();

        for (const QString& call : backend.calls)
            QVERIFY2(!call.startsWith(QStringLiteral("load")), qPrintable(call));
    }

    void rejectingTheSignerInstallsNothingAndSaysWhy()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        m.beginInstall(QStringLiteral("counter_ui"));
        backend.calls.clear();

        m.rejectSigner();

        QVERIFY(backend.calls.isEmpty());
        QVERIFY(!m.lastError().isEmpty());
        QVERIFY(m.signerPrompt().isEmpty());
    }

    void installingANativeOnlyRowIsRefusedWithoutAnyTraffic()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{nativeOnlyRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        backend.calls.clear();

        m.beginInstall(QStringLiteral("desktop_only"));

        QVERIFY(backend.calls.isEmpty());
        QCOMPARE(m.lastError(),
                 QStringLiteral("available on macOS and Linux, not in this build"));
    }

    void installingSomethingNotInTheCatalogIsRefusedByName()
    {
        // A view can outlive a refresh, so the name is re-checked rather than
        // trusted.
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();
        backend.calls.clear();

        m.beginInstall(QStringLiteral("never_published"));

        QVERIFY(backend.calls.isEmpty());
        QVERIFY(m.lastError().contains(QStringLiteral("not in this catalog")));
    }

    void anUnsignedPackageIsRefusedWithPackageManagersReason()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        backend.signerAnswer = unsignedVerdict();
        StoreAppManager m(&backend);
        m.refreshCatalog();

        m.beginInstall(QStringLiteral("counter_ui"));

        QVERIFY(m.signerPrompt().isEmpty());
        QCOMPARE(m.lastError(),
                 QStringLiteral("this package is unsigned and this build requires a signature"));
    }

    // THE `require` REFUSAL, AND THE ONE THING IT HAS TO SAY.
    //
    // This is the cell a Store shell exists for: signed, valid, and anchored to
    // nothing (ADR 0008). The refusal is correct -- but the ONLY way forward
    // from it is for that DID to enter this device's keyring, and a message
    // that does not name the DID leaves nobody able to take that step. The
    // Shell's log is where a developer reads what to pass to --trust-signer,
    // and a device has no `lgx keyring` to ask instead.
    void aSignerNoAnchorValidatesIsRefusedAndTheDidIsStillNamed()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        backend.signerAnswer = unanchoredSignerVerdict();
        StoreAppManager m(&backend);
        m.refreshCatalog();
        QSignalSpy logSpy(&m, &StoreAppManager::log);
        backend.calls.clear();

        m.beginInstall(QStringLiteral("counter_ui"));

        // Refused, and nothing installed.
        QVERIFY(!backend.calls.contains(QStringLiteral("install:/tmp/counter_ui.lgx")));
        QVERIFY(m.signerPrompt().isEmpty());
        QCOMPARE(m.lastError(),
                 QStringLiteral("signed by a key your keyring does not vouch for"));

        // ...and the DID survived, both as data a view can render and in the log.
        QCOMPARE(m.refusedSigner().value(QStringLiteral("signerDid")).toString(),
                 QStringLiteral("did:jwk:acme"));
        QString logged;
        for (const auto& call : logSpy)
            logged += call.at(0).toString() + QLatin1Char('\n');
        QVERIFY2(logged.contains(QStringLiteral("did:jwk:acme")),
                 qPrintable(QStringLiteral("the refusal did not name the DID; logged: ") + logged));
    }

    void anUnsignedRefusalHasNoDidToName()
    {
        // The control. `refusedSigner` reports what the verifier found, and an
        // unsigned package has no publisher to offer for anchoring.
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        backend.signerAnswer = unsignedVerdict();
        StoreAppManager m(&backend);
        m.refreshCatalog();

        m.beginInstall(QStringLiteral("counter_ui"));

        QVERIFY(m.refusedSigner().isEmpty());
    }

    void anInstallThatSucceedsLeavesNoRefusedSignerBehind()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        m.beginInstall(QStringLiteral("counter_ui"));
        m.approveSigner();

        QVERIFY(m.refusedSigner().isEmpty());
    }

    // ── consent ─────────────────────────────────────────────────────────────

    void anAnnouncementBecomesAPromptWithAQuestion()
    {
        FakeBackend backend;
        StoreAppManager m(&backend);
        QSignalSpy spy(&m, &StoreAppManager::consentPromptChanged);

        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        QCOMPARE(spy.count(), 1);
        const QVariantMap prompt = m.consentPrompt();
        QCOMPARE(prompt.value(QStringLiteral("caller")).toString(), QStringLiteral("counter_ui"));
        QCOMPARE(prompt.value(QStringLiteral("target")).toString(), QStringLiteral("chat_module"));
        QVERIFY(prompt.value(QStringLiteral("question")).toString()
                    .contains(QStringLiteral("you installed")));
    }

    void grantingRecordsTheDecisionWithCapabilityModule()
    {
        FakeBackend backend;
        StoreAppManager m(&backend);
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        m.answerConsent(true);

        QVERIFY(backend.calls.contains(
            QStringLiteral("decideConsent:counter_ui->chat_module=yes")));
        QVERIFY(m.consentPrompt().isEmpty());
    }

    void denyingRecordsADenialNotNothing()
    {
        FakeBackend backend;
        StoreAppManager m(&backend);
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        m.answerConsent(false);

        QVERIFY(backend.calls.contains(
            QStringLiteral("decideConsent:counter_ui->chat_module=no")));
    }

    void dismissingRecordsNothingAtAll()
    {
        // "Not now" is not "never", and a denial PERSISTS in capability_module --
        // so recording one here would make a dismissal permanent.
        FakeBackend backend;
        StoreAppManager m(&backend);
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        m.dismissConsent();

        QVERIFY(backend.calls.isEmpty());
        QVERIFY(m.consentPrompt().isEmpty());
    }

    void adecisionThatCouldNotBeRecordedIsReported()
    {
        // The user answered and nothing kept the answer. The module will ask
        // again, and a silent failure looks like a decision that did not stick
        // for no reason.
        FakeBackend backend;
        backend.consentRecordable = false;
        StoreAppManager m(&backend);
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        m.answerConsent(true);

        QVERIFY(m.lastError().contains(QStringLiteral("could not record")));
    }

    void twoPairsQueueAndAreAnsweredOneAtATime()
    {
        FakeBackend backend;
        StoreAppManager m(&backend);
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));
        m.onConsentRequired(
            consentRequired(QStringLiteral("counter_ui"), QStringLiteral("wallet_module")));

        QCOMPARE(m.pendingConsentCount(), 2);
        QCOMPARE(m.consentPrompt().value(QStringLiteral("target")).toString(),
                 QStringLiteral("chat_module"));

        m.answerConsent(true);

        QCOMPARE(m.pendingConsentCount(), 1);
        QCOMPARE(m.consentPrompt().value(QStringLiteral("target")).toString(),
                 QStringLiteral("wallet_module"));
    }

    void answeringNothingDoesNothing()
    {
        FakeBackend backend;
        StoreAppManager m(&backend);

        m.answerConsent(true);
        m.dismissConsent();

        QVERIFY(backend.calls.isEmpty());
    }

    // ── the two links ───────────────────────────────────────────────────────

    void thereportLinkIsHandedToThePlatform()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        QVERIFY(m.openReportLink(QStringLiteral("counter_ui")));

        QCOMPARE(backend.opened.size(), 1);
        QCOMPARE(backend.opened.at(0).toString(),
                 QStringLiteral("https://logos.test/report?m=counter_ui"));
    }

    void theuniversalLinkIsHandedToThePlatform()
    {
        FakeBackend backend;
        backend.catalog = QVariantList{webRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        QVERIFY(m.openUniversalLink(QStringLiteral("counter_ui")));

        QCOMPARE(backend.opened.size(), 1);
        QCOMPARE(backend.opened.at(0).toString(), QStringLiteral("https://logos.test/m/counter_ui"));
    }

    void arowWithNoLinksOpensNothingAndSaysSo()
    {
        FakeBackend backend;
        backend.catalog =
            QVariantList{catalogRow(QStringLiteral("plain"), true, QStringLiteral("ok"))};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        QVERIFY(!m.openReportLink(QStringLiteral("plain")));
        QVERIFY(backend.opened.isEmpty());
        QVERIFY(!m.lastError().isEmpty());
    }

    void arefusedLinkIsNeverHandedToThePlatform()
    {
        // The whole point of checking the scheme in CatalogEntry: by the time a
        // URL reaches here it has already been accepted, so a refused one has
        // nothing to hand over.
        FakeBackend backend;
        backend.catalog = QVariantList{catalogRow(
            QStringLiteral("evil"), true, QStringLiteral("ok"),
            QStringLiteral("file:///etc/passwd"), QStringLiteral("javascript:alert(1)"))};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        QVERIFY(!m.openReportLink(QStringLiteral("evil")));
        QVERIFY(!m.openUniversalLink(QStringLiteral("evil")));
        QVERIFY(backend.opened.isEmpty());
    }

    void anUnavailablePackageCanStillBeReported()
    {
        // "This catalog entry is lying about its platforms" is a report.
        FakeBackend backend;
        backend.catalog = QVariantList{nativeOnlyRow()};
        StoreAppManager m(&backend);
        m.refreshCatalog();

        QVERIFY(m.openReportLink(QStringLiteral("desktop_only")));
        QVERIFY(m.openUniversalLink(QStringLiteral("desktop_only")));
        QCOMPARE(backend.opened.size(), 2);
    }
};

QTEST_MAIN(StoreAppManagerTest)
#include "store_app_manager_test.moc"
