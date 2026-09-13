// srcdeps: appmanager/CatalogSource.cpp appmanager/CatalogEntry.cpp
//
// WHAT A STORE SHELL WAS POINTED AT (slice 29).
//
// The App Manager browses whatever repositories the device is configured with.
// Pointing one at a LOCAL catalog release -- the one a developer serves off
// their own machine while building it -- is the remaining half of the install
// criterion, and it arrives on the command line: a repository URL, the signer
// to anchor, and optionally the row to install.
//
// All three are somebody's typing, and two of them decide what this device will
// trust, which the rest of the App Manager already has rules for:
//
//   * a repository URL is handed to a fetcher, so it goes through the SAME
//     scheme rule a row's links do (CatalogEntry::linkRefusal) rather than a
//     second one that could be laxer;
//   * a trust anchor is a keyring entry, and anchoring a string that is not a
//     DID trusts nobody while looking like it trusted somebody.
//
// A refusal is RECORDED, not silent: a Store shell's whole diagnostic surface
// is one console, and a mistyped flag that simply does nothing is the worst of
// the three outcomes.
//
// Run: nix build .#unit-tests -L

#include "appmanager/CatalogSource.h"

#include <QtTest/QtTest>

using basecamp::appmanager::CatalogSource;

class CatalogSourceTest : public QObject
{
    Q_OBJECT

private slots:
    // A plain launch asks for nothing, and that is not an error.
    void PlainLaunchIsEmpty()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell")});
        QVERIFY(s.isEmpty());
        QVERIFY(s.refusals().isEmpty());
        QVERIFY(s.repositoryUrl().isEmpty());
        QVERIFY(s.installPackage().isEmpty());
        QVERIFY(s.trustAnchors().isEmpty());
    }

    // Anything this shell does not recognise belongs to Qt or to the platform.
    void UnknownArgumentsAreLeftAlone()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("-qmljsdebugger=port:1234"),
             QStringLiteral("--platform"), QStringLiteral("offscreen")});
        QVERIFY(s.isEmpty());
        QVERIFY(s.refusals().isEmpty());
    }

    void AnHttpsRepositoryIsAccepted()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("https://catalog.logos.co/logos-repo.json")});
        QCOMPARE(s.repositoryUrl(), QStringLiteral("https://catalog.logos.co/logos-repo.json"));
        QVERIFY(s.refusals().isEmpty());
        QVERIFY(!s.isEmpty());
    }

    // The whole reason this flag exists: a release a developer is serving off
    // their own machine, over plain http, to loopback and nowhere else.
    void ALoopbackHttpRepositoryIsAccepted()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("http://127.0.0.1:8099/logos-repo.json")});
        QCOMPARE(s.repositoryUrl(), QStringLiteral("http://127.0.0.1:8099/logos-repo.json"));
        QVERIFY(s.refusals().isEmpty());
    }

    // The SAME rule a row's report link goes through. A repository URL is
    // fetched rather than opened, but it is the same kind of input from the
    // same kind of author, and a second, laxer implementation of "is this URL
    // acceptable" is how the two drift apart.
    void APublicHttpRepositoryIsRefused()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("http://catalog.example.com/logos-repo.json")});
        QVERIFY(s.repositoryUrl().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.refusals().first().contains(
            QStringLiteral("http://catalog.example.com/logos-repo.json")));
    }

    void AFileUrlRepositoryIsRefused()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("file:///tmp/logos-repo.json")});
        QVERIFY(s.repositoryUrl().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
    }

    // A flag whose value is the next flag took the next flag as its value, and
    // then the real one was never parsed at all.
    void AFlagWithNoValueIsRefused()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository")});
        QVERIFY(s.repositoryUrl().isEmpty());
        QCOMPARE(s.refusals().size(), 1);

        const CatalogSource t = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("--install"), QStringLiteral("web_counter_b")});
        QVERIFY(t.repositoryUrl().isEmpty());
        QCOMPARE(t.installPackage(), QStringLiteral("web_counter_b"));
        QCOMPARE(t.refusals().size(), 1);
    }

    // One repository, and the FIRST one. Last-wins would silently discard the
    // URL the user is looking at in their own command line.
    void ASecondRepositoryIsRefused()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"),
             QStringLiteral("--repository"), QStringLiteral("https://a.example/logos-repo.json"),
             QStringLiteral("--repository"), QStringLiteral("https://b.example/logos-repo.json")});
        QCOMPARE(s.repositoryUrl(), QStringLiteral("https://a.example/logos-repo.json"));
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.refusals().first().contains(QStringLiteral("b.example")));
    }

    void ATrustAnchorIsNameAndDid()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--trust-signer"),
             QStringLiteral("logos-catalog-test=did:jwk:eyJrdHkiOiJPS1AifQ")});
        QCOMPARE(s.trustAnchors().size(), 1);
        QCOMPARE(s.trustAnchors().first().name, QStringLiteral("logos-catalog-test"));
        QCOMPARE(s.trustAnchors().first().did,
                 QStringLiteral("did:jwk:eyJrdHkiOiJPS1AifQ"));
        QVERIFY(s.refusals().isEmpty());
    }

    // A DID may itself contain '=' (did:jwk is base64url and a caller may pad
    // it), so the split is at the FIRST separator and everything after it is
    // the DID.
    void ADidKeepsItsOwnSeparators()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--trust-signer"),
             QStringLiteral("k=did:jwk:eyJrdHkiOiJPS1AifQ==")});
        QCOMPARE(s.trustAnchors().size(), 1);
        QCOMPARE(s.trustAnchors().first().did,
                 QStringLiteral("did:jwk:eyJrdHkiOiJPS1AifQ=="));
    }

    void SeveralAnchorsAreKeptInOrder()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"),
             QStringLiteral("--trust-signer"), QStringLiteral("a=did:jwk:one"),
             QStringLiteral("--trust-signer"), QStringLiteral("b=did:key:two")});
        QCOMPARE(s.trustAnchors().size(), 2);
        QCOMPARE(s.trustAnchors().at(0).name, QStringLiteral("a"));
        QCOMPARE(s.trustAnchors().at(1).name, QStringLiteral("b"));
        QVERIFY(s.refusals().isEmpty());
    }

    // ANCHORING A NON-DID TRUSTS NOBODY while looking like it trusted somebody.
    // The keyring would take the string, no signature would ever match it, and
    // every install would refuse under `require` with a message about the
    // package rather than about the keyring.
    void AnAnchorThatIsNotADidIsRefused()
    {
        const QStringList bad{QStringLiteral("k=Acme Modules"),
                              QStringLiteral("k="),
                              QStringLiteral("=did:jwk:x"),
                              QStringLiteral("did:jwk:x")};
        for (const QString& arg : bad) {
            const CatalogSource s = CatalogSource::fromArguments(
                {QStringLiteral("BasecampShell"), QStringLiteral("--trust-signer"), arg});
            QVERIFY2(s.trustAnchors().isEmpty(), qPrintable(arg));
            QCOMPARE(s.refusals().size(), 1);
        }
    }

    void AnInstallNamesOneRow()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"),
             QStringLiteral("--repository"), QStringLiteral("http://localhost:8099/logos-repo.json"),
             QStringLiteral("--trust-signer"), QStringLiteral("logos-catalog-test=did:jwk:abc"),
             QStringLiteral("--install"), QStringLiteral("web_counter_b")});
        QCOMPARE(s.installPackage(), QStringLiteral("web_counter_b"));
        QCOMPARE(s.repositoryUrl(), QStringLiteral("http://localhost:8099/logos-repo.json"));
        QCOMPARE(s.trustAnchors().size(), 1);
        QVERIFY(s.refusals().isEmpty());
        QVERIFY(!s.isEmpty());
    }

    // A refused flag still leaves the source non-empty: the user asked for
    // something, and a Shell that reported "nothing requested" would hide the
    // refusal it is holding.
    void ARefusalIsNotAnEmptySource()
    {
        const CatalogSource s = CatalogSource::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--repository"),
             QStringLiteral("javascript:alert(1)")});
        QVERIFY(!s.isEmpty());
        QCOMPARE(s.refusals().size(), 1);
    }
};

QTEST_MAIN(CatalogSourceTest)
#include "catalog_source_test.moc"
