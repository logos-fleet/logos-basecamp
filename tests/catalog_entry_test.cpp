// srcdeps: appmanager/CatalogEntry.cpp
//
// ONE CATALOG ROW, AS THE APP MANAGER SEES IT (slice 29).
//
// The App Manager on a Store shell renders a catalog it did not produce. Every
// row is somebody else's JSON, and three fields in it are things the Shell will
// ACT on: an install control, a report link and a universal link. This is where
// a row becomes something the Shell is willing to act on, and the two rules
// pinned here are the two ways it can be wrong:
//
//   A NATIVE-ONLY PACKAGE MUST HAVE NO INSTALL CONTROL, and must carry the
//   reason instead. A Store shell installs `web` variants and nothing else, so
//   most of a desktop catalog is uninstallable on one. A greyed-out button is
//   not an answer to "why can I not install this".
//
//   A LINK FROM A CATALOG MUST NOT BE HANDED TO THE PLATFORM UNCHECKED.
//   "The Shell opens both" means the Shell asks the OS to open a URL an index
//   author chose, and the set of schemes a phone does something surprising with
//   is open-ended. So the scheme is checked once, on the way in.
//
// Run: nix build .#unit-tests -L

#include "appmanager/CatalogEntry.h"

#include <QtTest/QtTest>

using basecamp::appmanager::CatalogEntry;
using basecamp::appmanager::entryFrom;
using basecamp::appmanager::linkRefusal;

namespace {

// The shape logos-package-downloader publishes, annotated by
// logos-package-manager's catalogAvailability.
QVariantMap row(const QString& name, const QVariantMap& availability,
                const QVariantMap& extra = {})
{
    QVariantMap versions;
    QVariantMap manifest;
    manifest[QStringLiteral("version")] = QStringLiteral("1.2.0");
    versions[QStringLiteral("manifest")] = manifest;

    QVariantMap r;
    r[QStringLiteral("name")] = name;
    r[QStringLiteral("repositoryUrl")] = QStringLiteral("https://logos.test/logos-repo.json");
    r[QStringLiteral("versions")] = QVariantList{versions};
    r[QStringLiteral("availability")] = availability;
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        r[it.key()] = it.value();
    return r;
}

QVariantMap availableAs(const QString& variant)
{
    return QVariantMap{
        {QStringLiteral("available"), true},
        {QStringLiteral("variant"), variant},
        {QStringLiteral("reason"), QStringLiteral("installable here as the 'web' variant")},
    };
}

QVariantMap unavailableBecause(const QString& reason)
{
    return QVariantMap{
        {QStringLiteral("available"), false},
        {QStringLiteral("variant"), QString()},
        {QStringLiteral("reason"), reason},
    };
}

} // namespace

class CatalogEntryTest : public QObject {
    Q_OBJECT

private slots:
    // ── the install control ─────────────────────────────────────────────────

    void aWebVariantIsOfferedAndNamesItsVariant()
    {
        const CatalogEntry e = entryFrom(row(QStringLiteral("counter_ui"), availableAs("web")));

        QVERIFY(e.available);
        QVERIFY(e.mayOfferInstall());
        QCOMPARE(e.variant, QStringLiteral("web"));
        QCOMPARE(e.version, QStringLiteral("1.2.0"));
        QVERIFY(e.unavailableReason.isEmpty());
    }

    void aNativeOnlyPackageIsNotOfferedAndCarriesTheReason()
    {
        const CatalogEntry e = entryFrom(row(
            QStringLiteral("desktop_only"),
            unavailableBecause(QStringLiteral("available on macOS and Linux, not in this build"))));

        QVERIFY(!e.available);
        QVERIFY(!e.mayOfferInstall());
        QCOMPARE(e.unavailableReason,
                 QStringLiteral("available on macOS and Linux, not in this build"));
        // No variant to name: there is nothing an install here would use.
        QVERIFY(e.variant.isEmpty());
    }

    void aRowCarriesTheDependenciesItsManifestDeclares()
    {
        // Carried through UNJUDGED, in both spellings an LGX manifest allows.
        // It is what the Platform floor walks (PlatformFloor, #169): whether a
        // Downloaded module may be offered at all depends on what it reaches,
        // and a row that dropped its dependency list here would be offered on a
        // shell that cannot run it.
        QVariantMap manifest;
        manifest[QStringLiteral("version")] = QStringLiteral("1.2.0");
        manifest[QStringLiteral("dependencies")] = QVariantList{
            QStringLiteral("delivery_module"),
            QVariantMap{{QStringLiteral("name"), QStringLiteral("capability_module")}},
            QStringLiteral(""),
        };
        QVariantMap r = row(QStringLiteral("chat_module"), availableAs("web"));
        r[QStringLiteral("versions")] =
            QVariantList{QVariantMap{{QStringLiteral("manifest"), manifest}}};

        const CatalogEntry e = entryFrom(r);

        QCOMPARE(e.dependencies, (QStringList{QStringLiteral("delivery_module"),
                                              QStringLiteral("capability_module")}));
    }

    void aRowWithNoAvailabilityVerdictIsNotOffered()
    {
        // The sharp one. An App Manager that read a MISSING annotation as "fine"
        // would offer an install for every native-only package in the catalog —
        // which is exactly the failure the annotation exists to prevent — and it
        // would do so silently, because the row otherwise looks complete.
        QVariantMap r = row(QStringLiteral("mystery"), QVariantMap{});
        r.remove(QStringLiteral("availability"));

        const CatalogEntry e = entryFrom(r);

        QVERIFY(!e.mayOfferInstall());
        QVERIFY(!e.unavailableReason.isEmpty());
    }

    void anAlreadyInstalledPackageIsNotOfferedAsAnInstall()
    {
        // Upgrading is a different control with a different confirmation, and an
        // Install button on something installed is how a user loses their data.
        QHash<QString, QString> installed;
        installed.insert(QStringLiteral("counter_ui"), QStringLiteral("1.0.0"));

        const CatalogEntry e =
            entryFrom(row(QStringLiteral("counter_ui"), availableAs("web")), installed);

        QVERIFY(e.available);
        QVERIFY(e.installed);
        QCOMPARE(e.installedVersion, QStringLiteral("1.0.0"));
        QVERIFY(!e.mayOfferInstall());
    }

    void theLabelFallsBackToTheModuleName()
    {
        const CatalogEntry bare =
            entryFrom(row(QStringLiteral("counter_ui"), availableAs("web")));
        QCOMPARE(bare.displayName, QStringLiteral("counter_ui"));

        const CatalogEntry named = entryFrom(row(
            QStringLiteral("counter_ui"), availableAs("web"),
            QVariantMap{{QStringLiteral("displayName"), QStringLiteral("Counter")}}));
        QCOMPARE(named.displayName, QStringLiteral("Counter"));
    }

    // ── the two links ───────────────────────────────────────────────────────

    void bothLinksSurviveWhenTheyAreHttps()
    {
        const CatalogEntry e = entryFrom(row(
            QStringLiteral("counter_ui"), availableAs("web"),
            QVariantMap{
                {QStringLiteral("reportUrl"), QStringLiteral("https://logos.test/report?m=counter_ui")},
                {QStringLiteral("universalLink"), QStringLiteral("https://logos.test/m/counter_ui")},
            }));

        QVERIFY(e.canReport());
        QVERIFY(e.canOpenUniversalLink());
        QCOMPARE(e.reportUrl.toString(), QStringLiteral("https://logos.test/report?m=counter_ui"));
        QCOMPARE(e.universalLink.toString(), QStringLiteral("https://logos.test/m/counter_ui"));
        QVERIFY(e.reportUrlRefusal.isEmpty());
        QVERIFY(e.universalLinkRefusal.isEmpty());
    }

    void aRowWithNoLinksHasNoAffordanceAndNoComplaint()
    {
        // Most of a catalog will carry neither, and a Shell logging that per row
        // would say nothing at all.
        const CatalogEntry e = entryFrom(row(QStringLiteral("counter_ui"), availableAs("web")));

        QVERIFY(!e.canReport());
        QVERIFY(!e.canOpenUniversalLink());
        QVERIFY(e.reportUrlRefusal.isEmpty());
        QVERIFY(e.universalLinkRefusal.isEmpty());
    }

    void aFileLinkIsRefusedAndSaysSo()
    {
        const CatalogEntry e = entryFrom(row(
            QStringLiteral("evil"), availableAs("web"),
            QVariantMap{{QStringLiteral("reportUrl"), QStringLiteral("file:///etc/passwd")}}));

        QVERIFY(!e.canReport());
        QVERIFY(e.reportUrl.isEmpty());
        // Non-empty, unlike the no-link case: a catalog doing this is worth a
        // line in the log.
        QVERIFY(!e.reportUrlRefusal.isEmpty());
        QVERIFY(e.reportUrlRefusal.contains(QStringLiteral("file")));
    }

    void everyNonHttpSchemeIsRefused()
    {
        const QStringList hostile{
            QStringLiteral("file:///etc/passwd"),
            QStringLiteral("javascript:alert(1)"),
            QStringLiteral("data:text/html,<script>x</script>"),
            QStringLiteral("about:blank"),
            // An app's own custom scheme, registered by whatever installed it.
            QStringLiteral("logos://module/index.html"),
            QStringLiteral("itms-services://?action=download-manifest"),
            QStringLiteral("tel:+15551234"),
        };
        for (const QString& raw : hostile)
            QVERIFY2(!linkRefusal(raw).isEmpty(), qPrintable(raw));
    }

    void plainHttpIsRefusedUnlessItIsLoopback()
    {
        // A report reaching its destination in clear text is the one thing a
        // report link must not do. Loopback stays, because a local catalog
        // release served while one is being built is exactly that.
        QVERIFY(!linkRefusal(QStringLiteral("http://logos.test/report")).isEmpty());
        QVERIFY(linkRefusal(QStringLiteral("http://localhost:8080/report")).isEmpty());
        QVERIFY(linkRefusal(QStringLiteral("http://127.0.0.1:8080/report")).isEmpty());
        QVERIFY(linkRefusal(QStringLiteral("https://logos.test/report")).isEmpty());
    }

    void aSchemeRelativeOrHostlessUrlIsRefused()
    {
        QVERIFY(!linkRefusal(QStringLiteral("/report")).isEmpty());
        QVERIFY(!linkRefusal(QStringLiteral("logos.test/report")).isEmpty());
        QVERIFY(!linkRefusal(QStringLiteral("https://")).isEmpty());
        QVERIFY(!linkRefusal(QString()).isEmpty());
    }

    void oneRefusedLinkDoesNotTakeTheOtherWithIt()
    {
        // The report affordance and the universal link are independent, and a
        // catalog that got one wrong should not lose both.
        const CatalogEntry e = entryFrom(row(
            QStringLiteral("counter_ui"), availableAs("web"),
            QVariantMap{
                {QStringLiteral("reportUrl"), QStringLiteral("javascript:alert(1)")},
                {QStringLiteral("universalLink"), QStringLiteral("https://logos.test/m/counter_ui")},
            }));

        QVERIFY(!e.canReport());
        QVERIFY(e.canOpenUniversalLink());
    }

    void anUnavailablePackageStillCarriesItsLinks()
    {
        // Reporting a module you cannot install is a legitimate thing to want:
        // "this catalog entry is lying about its platforms" is a report.
        const CatalogEntry e = entryFrom(row(
            QStringLiteral("desktop_only"),
            unavailableBecause(QStringLiteral("available on macOS, not in this build")),
            QVariantMap{
                {QStringLiteral("reportUrl"), QStringLiteral("https://logos.test/report?m=desktop_only")},
                {QStringLiteral("universalLink"), QStringLiteral("https://logos.test/m/desktop_only")},
            }));

        QVERIFY(!e.mayOfferInstall());
        QVERIFY(e.canReport());
        QVERIFY(e.canOpenUniversalLink());
    }
};

QTEST_MAIN(CatalogEntryTest)
#include "catalog_entry_test.moc"
