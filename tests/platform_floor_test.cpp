// srcdeps: appmanager/PlatformFloor.cpp
//
// EACH SHELL'S PLATFORM FLOOR, DERIVED FROM THE CATALOG IT OFFERS (#169).
//
// ADR 0009's second rule, and the mirror of ADR 0007's "a Bundled module may
// depend only on Bundled modules": a Downloaded module may depend on a Platform
// module only where that module is in the Bundled set of the shell it lands on.
//
// The floor itself is DERIVED AT BUILD TIME -- nix/platform-floor.nix walks the
// mobile catalog, and nix/bundled-set.nix writes the answer into
// bundled-set.json -- because a hand-kept list is a third home for a truth that
// already lives in metadata.json and in the shell's bundle set, and it drifts
// into exactly the failure ADR 0007's honest-availability rule exists to
// prevent: a module offered for install that dies at load.
//
// This is the runtime half: the floor as the App Manager reads it back out of
// the manifest its build compiled in, and the verdict it produces for a catalog
// row. Two things are pinned here and they are the two ways it can be wrong:
//
//   A ROW THAT REACHES AN ABSENT PLATFORM MODULE IS REFUSED BY NAME, and the
//   walk is TRANSITIVE. chat_module is deliberately not a Platform module
//   itself -- its network comes from delivery_module -- so a floor that only
//   looked at a row's own flag would offer chat_ui on a shell that cannot run
//   a single one of its messages.
//
//   A BUILD THAT DECLARED NO FLOOR REFUSES NOTHING. The manifest is the app
//   image's own file and an older one has no `platformFloor` key at all;
//   reading that as "every platform module is missing" would empty the catalog
//   of a shell that is working perfectly.
//
// Run: nix build .#unit-tests -L

#include "appmanager/PlatformFloor.h"

#include <QtTest/QtTest>

using basecamp::appmanager::PlatformFloor;

namespace {

// What nix/bundled-set.nix writes, reduced to the keys this reads.
QByteArray manifest(const QString& body)
{
    return QStringLiteral(R"({"bundledSetVersion":"1","target":"ios-sim-arm64",
        "modules":[{"name":"chat_module","version":"0.2.2"}], %1})")
        .arg(body)
        .toUtf8();
}

// name -> its declared dependencies, the way a catalog's manifests read.
QHash<QString, QStringList> graph()
{
    QHash<QString, QStringList> deps;
    deps.insert(QStringLiteral("chat_ui"), {QStringLiteral("chat_module")});
    deps.insert(QStringLiteral("chat_module"), {QStringLiteral("delivery_module")});
    deps.insert(QStringLiteral("wallet_ui"), {QStringLiteral("eth_rpc_module")});
    return deps;
}

} // namespace

class PlatformFloorTest : public QObject {
    Q_OBJECT

private slots:
    // ── the floor, as the build wrote it ────────────────────────────────────

    void theFloorIsReadOutOfTheBundledSetManifest()
    {
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":["eth_rpc_module"],"absent":["delivery_module"]})"));

        QVERIFY(floor.isDeclared());
        QCOMPARE(floor.present(), QStringList{QStringLiteral("eth_rpc_module")});
        QCOMPARE(floor.absent(), QStringList{QStringLiteral("delivery_module")});
    }

    void aManifestWithNoFloorDeclaresNone()
    {
        // A build that predates the derivation, or one whose catalog names no
        // Platform module at all. Refusing the catalog on that basis would take
        // a working shell's App Manager away.
        const PlatformFloor floor =
            PlatformFloor::fromBundledSetManifest(manifest(R"("requested":["chat_ui"])"));

        QVERIFY(!floor.isDeclared());
        QVERIFY(floor.missingFor(QStringLiteral("chat_ui"), graph()).isEmpty());
    }

    void unreadableJsonDeclaresNoFloorRatherThanAnEmptyOne()
    {
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest("{not json");
        QVERIFY(!floor.isDeclared());
    }

    // ── the verdict ─────────────────────────────────────────────────────────

    void aDirectDependencyOnAnAbsentPlatformModuleIsNamed()
    {
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":[],"absent":["delivery_module"]})"));

        QCOMPARE(floor.missingFor(QStringLiteral("chat_module"), graph()),
                 QStringLiteral("delivery_module"));
        QCOMPARE(PlatformFloor::reasonFor(QStringLiteral("delivery_module")),
                 QStringLiteral("requires delivery_module, not in this build"));
    }

    void theWalkIsTransitive()
    {
        // THE SHARP ONE, and the reason the floor is derived rather than
        // declared. chat_ui declares chat_module and nothing else; chat_module
        // is not a Platform module. The Platform module is one edge further
        // down, and a shell that omitted it would otherwise offer chat_ui an
        // install that installs and then cannot send a message.
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":[],"absent":["delivery_module"]})"));

        QCOMPARE(floor.missingFor(QStringLiteral("chat_ui"), graph()),
                 QStringLiteral("delivery_module"));
    }

    void aPlatformModuleThisBuildShipsIsNotMissing()
    {
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":["delivery_module","eth_rpc_module"],"absent":[]})"));

        QVERIFY(floor.missingFor(QStringLiteral("chat_ui"), graph()).isEmpty());
        QVERIFY(floor.missingFor(QStringLiteral("wallet_ui"), graph()).isEmpty());
    }

    void aDependencyTheFloorHasNeverHeardOfIsNotRefused()
    {
        // The floor answers ONE question -- "is this Platform module in this
        // build" -- and a name it does not carry is not a Platform module as far
        // as this catalog knows. Whether such a module ships a variant this
        // shell can install is package_manager's verdict, and answering it here
        // a second time is how the two come to disagree.
        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":[],"absent":["delivery_module"]})"));

        QVERIFY(floor.missingFor(QStringLiteral("wallet_ui"), graph()).isEmpty());
    }

    void aDependencyCycleTerminates()
    {
        QHash<QString, QStringList> cyclic;
        cyclic.insert(QStringLiteral("a"), {QStringLiteral("b")});
        cyclic.insert(QStringLiteral("b"), {QStringLiteral("a"), QStringLiteral("delivery_module")});

        const PlatformFloor floor = PlatformFloor::fromBundledSetManifest(manifest(
            R"("platformFloor":{"present":[],"absent":["delivery_module"]})"));

        QCOMPARE(floor.missingFor(QStringLiteral("a"), cyclic),
                 QStringLiteral("delivery_module"));
    }
};

QTEST_MAIN(PlatformFloorTest)
#include "platform_floor_test.moc"
