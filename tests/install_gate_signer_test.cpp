// srcdeps: appmanager/InstallGate.cpp appmanager/CatalogEntry.cpp
//
// THE ORDER AN APP MANAGER INSTALLS IN (slice 29).
//
// Installing a Downloaded module from a catalog is five steps and four of them
// can refuse. The ORDER is the interesting part, and two orderings are wrong in
// ways a casual reading does not catch:
//
//   Installing and THEN showing the signer. A prompt after the fact is not
//   consent; the package is already on disk and already loadable.
//
//   Prompting BEFORE the download. There is no signer to show — a catalog's
//   advertised DID is a claim about bytes that have not arrived, and the
//   criterion is that the prompt shows the PACKAGE's signer.
//
// So each test here asserts what was asked, in which order, and what the gate
// did with the answer. Everything that touches a module is behind
// InstallGate::Modules, which is what lets that happen on a desktop with no
// core, no network and no catalog.
//
// Run: nix build .#unit-tests -L

#include "appmanager/CatalogEntry.h"
#include "appmanager/InstallGate.h"

#include <QtTest/QtTest>

using basecamp::appmanager::CatalogEntry;
using basecamp::appmanager::InstallGate;

namespace {

// A recording stand-in for the two package modules. Each answer is settable and
// every call is appended to `calls`, so a test can assert the sequence rather
// than only the outcome.
class FakeModules : public InstallGate::Modules {
public:
    QStringList calls;

    QVariantMap downloadAnswer{
        {QStringLiteral("success"), true},
        {QStringLiteral("path"), QStringLiteral("/tmp/counter_ui.lgx")},
    };
    QVariantMap signerAnswer{
        {QStringLiteral("name"), QStringLiteral("counter_ui")},
        {QStringLiteral("version"), QStringLiteral("1.2.0")},
        {QStringLiteral("signatureStatus"), QStringLiteral("signed")},
        {QStringLiteral("signerName"), QStringLiteral("Acme Modules")},
        {QStringLiteral("signerDid"), QStringLiteral("did:jwk:eyJjcnYiOiJFZDI1NTE5")},
        {QStringLiteral("trusted"), true},
        {QStringLiteral("trustedAs"), QStringLiteral("acme")},
        {QStringLiteral("installable"), true},
        {QStringLiteral("reason"), QStringLiteral("signed by 'acme', a publisher you trust")},
    };
    QVariantMap installAnswer{
        {QStringLiteral("name"), QStringLiteral("counter_ui")},
        {QStringLiteral("path"), QStringLiteral("/data/modules/counter_ui/index.js")},
    };

    QVariantMap downloadPinned(const QString& repositoryUrl, const QString& packageName,
                               const QString& version) override
    {
        calls << QStringLiteral("download:%1@%2 from %3")
                     .arg(packageName, version, repositoryUrl);
        return downloadAnswer;
    }

    QVariantMap signerTrust(const QString& lgxPath) override
    {
        calls << QStringLiteral("signerTrust:%1").arg(lgxPath);
        return signerAnswer;
    }

    QVariantMap installPlugin(const QString& lgxPath) override
    {
        calls << QStringLiteral("install:%1").arg(lgxPath);
        return installAnswer;
    }
};

CatalogEntry installableRow()
{
    CatalogEntry e;
    e.name = QStringLiteral("counter_ui");
    e.displayName = QStringLiteral("Counter");
    e.version = QStringLiteral("1.2.0");
    e.repositoryUrl = QStringLiteral("https://logos.test/logos-repo.json");
    e.available = true;
    e.variant = QStringLiteral("web");
    return e;
}

} // namespace

class InstallGateSignerTest : public QObject {
    Q_OBJECT

private slots:
    // ── the happy path, and its order ───────────────────────────────────────

    void theSignerPromptComesAfterTheDownloadAndBeforeTheInstall()
    {
        FakeModules modules;
        InstallGate gate(&modules);

        QVERIFY(gate.begin(installableRow()));

        // Downloaded and asked about; NOT installed.
        QCOMPARE(modules.calls.size(), 2);
        QVERIFY(modules.calls.at(0).startsWith(QStringLiteral("download:counter_ui@1.2.0")));
        QCOMPARE(modules.calls.at(1), QStringLiteral("signerTrust:/tmp/counter_ui.lgx"));
        QCOMPARE(gate.stage(), InstallGate::Stage::AwaitingSigner);

        // And it stays that way until a person answers. This is the assertion
        // the whole class exists for.
        QVERIFY(!modules.calls.contains(QStringLiteral("install:/tmp/counter_ui.lgx")));
    }

    void thePromptCarriesTheSignerNameAndDid()
    {
        FakeModules modules;
        InstallGate gate(&modules);
        QVERIFY(gate.begin(installableRow()));

        const QVariantMap prompt = gate.signerPrompt();
        QCOMPARE(prompt.value(QStringLiteral("signerName")).toString(),
                 QStringLiteral("Acme Modules"));
        QVERIFY(prompt.value(QStringLiteral("signerDid")).toString()
                    .startsWith(QStringLiteral("did:jwk:")));
        // Both, together: a name is what the publisher chose to call themselves,
        // and the DID is the thing the keyring was checked against. A prompt
        // with only the name shows the user nothing they can verify.
        QCOMPARE(prompt.value(QStringLiteral("name")).toString(), QStringLiteral("counter_ui"));
        QCOMPARE(prompt.value(QStringLiteral("version")).toString(), QStringLiteral("1.2.0"));
    }

    void approvingInstalls()
    {
        FakeModules modules;
        InstallGate gate(&modules);
        QVERIFY(gate.begin(installableRow()));

        QVERIFY(gate.approve());

        QCOMPARE(gate.stage(), InstallGate::Stage::Installed);
        QCOMPARE(modules.calls.size(), 3);
        QCOMPARE(modules.calls.at(2), QStringLiteral("install:/tmp/counter_ui.lgx"));
        QCOMPARE(gate.installedPath(), QStringLiteral("/data/modules/counter_ui/index.js"));
        QVERIFY(gate.error().isEmpty());
    }

    void rejectingInstallsNothing()
    {
        FakeModules modules;
        InstallGate gate(&modules);
        QVERIFY(gate.begin(installableRow()));

        gate.reject();

        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QCOMPARE(modules.calls.size(), 2);   // download + signerTrust, no install
        QVERIFY(!gate.error().isEmpty());
        QVERIFY(gate.installedPath().isEmpty());
    }

    // ── the refusals, in the order they happen ──────────────────────────────

    void anUnavailableRowIsRefusedWithoutTouchingTheNetwork()
    {
        // The control should not have existed. A gate that trusted its caller
        // would download and install a native-only package on a phone if one
        // button was ever mis-wired — so it refuses BEFORE any traffic.
        FakeModules modules;
        InstallGate gate(&modules);

        CatalogEntry e = installableRow();
        e.available = false;
        e.unavailableReason = QStringLiteral("available on macOS and Linux, not in this build");

        QVERIFY(!gate.begin(e));

        QVERIFY(modules.calls.isEmpty());
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QCOMPARE(gate.error(), QStringLiteral("available on macOS and Linux, not in this build"));
    }

    void anAlreadyInstalledRowIsRefusedWithoutTouchingTheNetwork()
    {
        FakeModules modules;
        InstallGate gate(&modules);

        CatalogEntry e = installableRow();
        e.installed = true;
        e.installedVersion = QStringLiteral("1.0.0");

        QVERIFY(!gate.begin(e));
        QVERIFY(modules.calls.isEmpty());
        QVERIFY(gate.error().contains(QStringLiteral("already installed")));
    }

    // THE DOWNLOADER'S ACTUAL CONTRACT, which has no `success` key in it.
    //
    // package_downloader.downloadPinned answers `{ name, path, ... }` or
    // `{ name, error }` — the same "success is the ABSENCE of error" shape
    // installPlugin has, and the same one this gate already reads for the
    // install step. Reading a `success` the module never writes made
    // `value("success", false)` false for every download that had just
    // WORKED: measured on an iPad simulator, 2026-09-13, where a 1.3 MB
    // package downloaded, verified against the index, and was refused one
    // line later as "downloading 'web_counter_b' failed".
    void aDownloadWithNoSuccessKeyIsStillADownload()
    {
        FakeModules modules;
        modules.downloadAnswer = QVariantMap{
            {QStringLiteral("name"), QStringLiteral("counter_ui")},
            {QStringLiteral("path"), QStringLiteral("/tmp/counter_ui.lgx")},
        };
        InstallGate gate(&modules);

        QVERIFY(gate.begin(installableRow()));
        QCOMPARE(gate.stage(), InstallGate::Stage::AwaitingSigner);
        QVERIFY(modules.calls.contains(QStringLiteral("signerTrust:/tmp/counter_ui.lgx")));
    }

    // ...and the failing half of the same contract: an `error` and no `path`.
    void aDownloadThatOnlyReportsAnErrorIsRefused()
    {
        FakeModules modules;
        modules.downloadAnswer = QVariantMap{
            {QStringLiteral("name"), QStringLiteral("counter_ui")},
            {QStringLiteral("error"),
             QStringLiteral("download failed for 'counter_ui' — index fetch failed")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));
        QCOMPARE(modules.calls.size(), 1);
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QVERIFY(gate.error().contains(QStringLiteral("index fetch failed")));
        QVERIFY(gate.signerPrompt().isEmpty());
    }

    void aFailedDownloadNeverReachesTheSignerPrompt()
    {
        FakeModules modules;
        modules.downloadAnswer = QVariantMap{
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("index fetch failed")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));

        QCOMPARE(modules.calls.size(), 1);
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QCOMPARE(gate.error(), QStringLiteral("index fetch failed"));
        QVERIFY(gate.signerPrompt().isEmpty());
    }

    void aDownloadThatReportsSuccessWithNoFileIsRefused()
    {
        // Installing "" would ask package_manager to read a directory and report
        // a confusing error two steps from the cause.
        FakeModules modules;
        modules.downloadAnswer = QVariantMap{{QStringLiteral("name"),
                                              QStringLiteral("counter_ui")}};
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));

        QCOMPARE(modules.calls.size(), 1);
        QVERIFY(gate.error().contains(QStringLiteral("no file")));
    }

    void anUnsignedPackageIsNotPromptedAboutAtAll()
    {
        // The criterion: an unsigned package cannot be installed. There is also
        // no prompt to show — no signer, nothing for the user to decide — so the
        // gate refuses rather than offering a dialog whose only answer is no.
        FakeModules modules;
        modules.signerAnswer = QVariantMap{
            {QStringLiteral("signatureStatus"), QStringLiteral("unsigned")},
            {QStringLiteral("installable"), false},
            {QStringLiteral("reason"),
             QStringLiteral("this package is unsigned and this build requires a signature")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));

        QCOMPARE(modules.calls.size(), 2);   // downloaded, asked, refused
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QVERIFY(gate.error().contains(QStringLiteral("unsigned")));
        QVERIFY(gate.signerPrompt().isEmpty());
    }

    void aMismatchedSignatureIsRefusedForTheReasonPackageManagerGave()
    {
        FakeModules modules;
        modules.signerAnswer = QVariantMap{
            {QStringLiteral("signatureStatus"), QStringLiteral("invalid")},
            {QStringLiteral("signerDid"), QStringLiteral("did:jwk:whoever")},
            {QStringLiteral("installable"), false},
            {QStringLiteral("reason"),
             QStringLiteral("the signature on this package does not verify")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));
        QCOMPARE(gate.error(), QStringLiteral("the signature on this package does not verify"));
    }

    // ── THE `require` REFUSAL, WHICH IS WHAT A STORE SHELL IS FOR ───────────
    //
    // `ShellStoreBackend::configure` sets setSignaturePolicy("require"), and
    // ADR 0008 fixes the anchor set: the local keyring, populated only by an
    // explicit act. So the cell that decides whether the gate is ENFORCED or
    // merely ASSUMED is "signed, valid, and anchored to nothing" — and until
    // this test existed nothing in this repo drove it. The neighbouring test
    // above is the desktop's `warn`, which INSTALLS.
    void underRequireASignerTheKeyringDoesNotVouchForIsRefused()
    {
        FakeModules modules;
        modules.signerAnswer = QVariantMap{
            {QStringLiteral("name"), QStringLiteral("counter_ui")},
            {QStringLiteral("version"), QStringLiteral("1.2.0")},
            {QStringLiteral("signatureStatus"), QStringLiteral("signed")},
            {QStringLiteral("signerName"), QStringLiteral("Acme Modules")},
            {QStringLiteral("signerDid"), QStringLiteral("did:jwk:eyJjcnYiOiJFZDI1NTE5")},
            {QStringLiteral("trusted"), false},
            {QStringLiteral("trustedAs"), QString()},
            {QStringLiteral("policy"), QStringLiteral("require")},
            {QStringLiteral("installable"), false},
            {QStringLiteral("reason"),
             QStringLiteral("signed by a key your keyring does not vouch for")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));

        QCOMPARE(modules.calls.size(), 2);   // downloaded, asked, refused
        QVERIFY(!modules.calls.contains(QStringLiteral("install:/tmp/counter_ui.lgx")));
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QVERIFY(gate.error().contains(QStringLiteral("does not vouch")));
        // No prompt: there is nothing to approve. A dialog whose only answer is
        // no is not consent, it is a dead end with a button on it.
        QVERIFY(gate.signerPrompt().isEmpty());
    }

    void aRefusedRequireVerdictCannotBeApprovedPastTheGate()
    {
        // The control on the test above. A gate that refused by REPORTING and
        // then left the flow armed would install on the next tap of Install,
        // which is the same defect as never having refused.
        FakeModules modules;
        modules.signerAnswer[QStringLiteral("installable")] = false;
        modules.signerAnswer[QStringLiteral("trusted")] = false;
        modules.signerAnswer[QStringLiteral("trustedAs")] = QString();
        modules.signerAnswer[QStringLiteral("reason")] =
            QStringLiteral("signed by a key your keyring does not vouch for");
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));
        QVERIFY(!gate.approve());

        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QVERIFY(!modules.calls.contains(QStringLiteral("install:/tmp/counter_ui.lgx")));
        QVERIFY(gate.installedPath().isEmpty());
    }

    // THE DID HAS TO SURVIVE THE REFUSAL, and this is the "who anchors, on a
    // phone" question (#103) stated as an assertion.
    //
    // Under `require` the ONLY way forward from this refusal is for the DID to
    // enter this device's keyring. package_manager keeps the name and the DID in
    // its answer for exactly that reason -- "hiding them would remove the only
    // way forward" (logos-package-manager-module, test_signer_trust.cpp) -- and
    // one layer down the library's own REQUIRE refusal names the DID in its
    // error string. The gate threw both away: `refuse()` clears the prompt, and
    // the Shell was left holding a sentence about a key it could not name.
    void aRefusalUnderRequireStillNamesTheSignerItRefused()
    {
        FakeModules modules;
        modules.signerAnswer[QStringLiteral("installable")] = false;
        modules.signerAnswer[QStringLiteral("trusted")] = false;
        modules.signerAnswer[QStringLiteral("trustedAs")] = QString();
        modules.signerAnswer[QStringLiteral("reason")] =
            QStringLiteral("signed by a key your keyring does not vouch for");
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));

        const QVariantMap refused = gate.refusedSigner();
        QCOMPARE(refused.value(QStringLiteral("signerDid")).toString(),
                 QStringLiteral("did:jwk:eyJjcnYiOiJFZDI1NTE5"));
        QCOMPARE(refused.value(QStringLiteral("signerName")).toString(),
                 QStringLiteral("Acme Modules"));
        QCOMPARE(refused.value(QStringLiteral("name")).toString(),
                 QStringLiteral("counter_ui"));
        // ...and it is still NOT the prompt. Naming what was refused must not
        // put an Install button back on screen.
        QVERIFY(gate.signerPrompt().isEmpty());
    }

    void anUnsignedRefusalNamesNoSignerBecauseThereIsNone()
    {
        // The control that keeps `refusedSigner` honest: it reports what the
        // verifier found, not a placeholder. An unsigned package has no DID to
        // anchor, so there is nothing here for a trust affordance to offer.
        FakeModules modules;
        modules.signerAnswer = QVariantMap{
            {QStringLiteral("name"), QStringLiteral("counter_ui")},
            {QStringLiteral("signatureStatus"), QStringLiteral("unsigned")},
            {QStringLiteral("installable"), false},
            {QStringLiteral("reason"),
             QStringLiteral("this package is unsigned and this build requires a signature")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));
        QVERIFY(gate.refusedSigner().isEmpty());
    }

    void arefusalBeforeTheSignerStepNamesNoSigner()
    {
        // Nothing was ever asked, so there is nothing to report. A gate that
        // carried a stale signer from a previous row here would offer the user
        // somebody else's DID to anchor.
        FakeModules modules;
        modules.downloadAnswer = QVariantMap{
            {QStringLiteral("name"), QStringLiteral("counter_ui")},
            {QStringLiteral("error"), QStringLiteral("index fetch failed")},
        };
        InstallGate gate(&modules);

        QVERIFY(!gate.begin(installableRow()));
        QVERIFY(gate.refusedSigner().isEmpty());
    }

    void aSecondBeginClearsTheFirstRefusedSigner()
    {
        FakeModules modules;
        modules.signerAnswer[QStringLiteral("installable")] = false;
        modules.signerAnswer[QStringLiteral("reason")] =
            QStringLiteral("signed by a key your keyring does not vouch for");
        InstallGate gate(&modules);
        QVERIFY(!gate.begin(installableRow()));
        QVERIFY(!gate.refusedSigner().isEmpty());

        FakeModules fresh;
        InstallGate ok(&fresh);
        QVERIFY(ok.begin(installableRow()));
        // ...and on the gate that refused, a successful second begin clears it.
        modules.signerAnswer[QStringLiteral("installable")] = true;
        QVERIFY(gate.begin(installableRow()));
        QVERIFY(gate.refusedSigner().isEmpty());
    }

    void anUntrustedButInstallableSignerStillPrompts()
    {
        // Under the desktop's `warn` policy a signature from an unknown publisher
        // installs — with a prompt saying exactly that. The gate does not
        // second-guess package_manager's verdict in either direction.
        FakeModules modules;
        modules.signerAnswer[QStringLiteral("trusted")] = false;
        modules.signerAnswer[QStringLiteral("trustedAs")] = QString();
        modules.signerAnswer[QStringLiteral("reason")] =
            QStringLiteral("signed, by a publisher you have not marked as trusted");
        InstallGate gate(&modules);

        QVERIFY(gate.begin(installableRow()));
        QCOMPARE(gate.stage(), InstallGate::Stage::AwaitingSigner);
        QVERIFY(!gate.signerPrompt().value(QStringLiteral("trusted")).toBool());
        QVERIFY(gate.approve());
    }

    void aFailedInstallIsReportedRatherThanClaimed()
    {
        // installPlugin's contract is that success is the ABSENCE of `error`,
        // which is easy to get backwards — and a gate that got it backwards
        // would report every failure as an install.
        FakeModules modules;
        modules.installAnswer = QVariantMap{
            {QStringLiteral("error"), QStringLiteral("Package signed by untrusted key")},
        };
        InstallGate gate(&modules);
        QVERIFY(gate.begin(installableRow()));

        QVERIFY(!gate.approve());

        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
        QCOMPARE(gate.error(), QStringLiteral("Package signed by untrusted key"));
        QVERIFY(gate.installedPath().isEmpty());
    }

    // ── nothing happens out of turn ─────────────────────────────────────────

    void approvingWithNothingPendingInstallsNothing()
    {
        FakeModules modules;
        InstallGate gate(&modules);

        QVERIFY(!gate.approve());
        QVERIFY(modules.calls.isEmpty());
    }

    void approvingTwiceInstallsOnce()
    {
        FakeModules modules;
        InstallGate gate(&modules);
        QVERIFY(gate.begin(installableRow()));
        QVERIFY(gate.approve());

        QVERIFY(!gate.approve());
        QCOMPARE(modules.calls.count(QStringLiteral("install:/tmp/counter_ui.lgx")), 1);
    }

    void aSecondBeginClearsTheFirstRefusal()
    {
        // The App Manager is a list: a user who is refused one row and taps
        // another must not see the first row's error on the second one.
        FakeModules modules;
        CatalogEntry unavailable = installableRow();
        unavailable.available = false;
        unavailable.unavailableReason = QStringLiteral("not in this build");

        InstallGate gate(&modules);
        QVERIFY(!gate.begin(unavailable));
        QVERIFY(!gate.error().isEmpty());

        QVERIFY(gate.begin(installableRow()));
        QVERIFY(gate.error().isEmpty());
        QCOMPARE(gate.stage(), InstallGate::Stage::AwaitingSigner);
    }

    void aGateWithNoModulesRefusesRatherThanCrashes()
    {
        InstallGate gate(nullptr);
        QVERIFY(!gate.begin(installableRow()));
        QCOMPARE(gate.stage(), InstallGate::Stage::Refused);
    }
};

QTEST_MAIN(InstallGateSignerTest)
#include "install_gate_signer_test.moc"
