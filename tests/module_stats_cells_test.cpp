// srcdeps: ModuleStatsCells.cpp
//
// THE TWO FIGURES A MODULES-TAB ROW SHOWS, and where they come from.
//
// The runtime under a Basecamp host is not one thing. The SDK facade renames
// what the core emits (`cpuPercent`, `memoryMb`); the phone's Shell hands the
// core's JSON through untouched (`cpu_percent`, `memory_mb`); older hosts wrote
// `cpu` and `memory`. Reading only the facade's names is how every Bundled
// module on a phone came to show 0.0% / 0.0 MB whatever it was doing (#86):
// the keys the runtime actually emitted were never looked at, and a missing key
// reads as zero.
//
// The other half is that a MISSING measurement and a measurement of zero are
// different facts. A module the runtime could not measure (it has no process
// and its container cannot account for it) reports null, and rendering that as
// "0.0 MB" is a claim nobody made.
//
// Run: nix build .#unit-tests -L

#include "ModuleStatsCells.h"

#include <QtTest/QtTest>

using basecamp::ModuleStatsCells;
using basecamp::moduleStatsCells;

class ModuleStatsCellsTest : public QObject
{
    Q_OBJECT

private slots:
    void readsTheSdkFacadesNames()
    {
        QVariantMap entry;
        entry[QStringLiteral("name")] = QStringLiteral("chat_module");
        entry[QStringLiteral("cpuPercent")] = 3.24;
        entry[QStringLiteral("memoryMb")] = 61.5;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(cells.measured);
        QCOMPARE(cells.cpu, QStringLiteral("3.2"));
        QCOMPARE(cells.memory, QStringLiteral("61.5"));
    }

    void readsTheCoresOwnNames()
    {
        // What a phone's Shell hands through: liblogos' JSON, unrenamed. This
        // is the spelling that was never read, and #86's whole symptom.
        QVariantMap entry;
        entry[QStringLiteral("name")] = QStringLiteral("capability_module");
        entry[QStringLiteral("cpu_percent")] = 12.5;
        entry[QStringLiteral("memory_mb")] = 3.81;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(cells.measured);
        QCOMPARE(cells.cpu, QStringLiteral("12.5"));
        QCOMPARE(cells.memory, QStringLiteral("3.8"));
    }

    void readsTheOlderSpellings()
    {
        QVariantMap entry;
        entry[QStringLiteral("cpu")] = 1.0;
        entry[QStringLiteral("memory_MB")] = 8.0;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(cells.measured);
        QCOMPARE(cells.cpu, QStringLiteral("1.0"));
        QCOMPARE(cells.memory, QStringLiteral("8.0"));
    }

    void aRealZeroIsAMeasurementAndIsKept()
    {
        // The rule this replaces took "the first NON-ZERO hit", so a module
        // that really was idle fell through to a spelling nobody emits and
        // ended up reading the same zero by accident. Presence is the test, not
        // magnitude.
        QVariantMap entry;
        entry[QStringLiteral("cpu_percent")] = 0.0;
        entry[QStringLiteral("memory_mb")] = 0.0;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(cells.measured);
        QCOMPARE(cells.cpu, QStringLiteral("0.0"));
        QCOMPARE(cells.memory, QStringLiteral("0.0"));
    }

    void aNullFigureIsNotAMeasurement()
    {
        // liblogos reports null for a module nothing could measure, and it does
        // that instead of zero precisely so this case is distinguishable.
        QVariantMap entry;
        entry[QStringLiteral("name")] = QStringLiteral("unmeasurable");
        entry[QStringLiteral("cpu_percent")] = QVariant();
        entry[QStringLiteral("memory_mb")] = QVariant();

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(!cells.measured);
        QVERIFY(cells.cpu.isEmpty());
        QVERIFY(cells.memory.isEmpty());
    }

    void anEntryWithNoFiguresAtAllIsNotAMeasurement()
    {
        QVariantMap entry;
        entry[QStringLiteral("name")] = QStringLiteral("nothing_here");

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(!cells.measured);
    }

    void theFacadesNameWinsOverTheCores()
    {
        // Both can be present: the facade passes every raw key through
        // alongside its own. They are the same number; taking the facade's
        // keeps the host that renamed them authoritative.
        QVariantMap entry;
        entry[QStringLiteral("cpuPercent")] = 5.0;
        entry[QStringLiteral("cpu_percent")] = 9.0;
        entry[QStringLiteral("memoryMb")] = 2.0;
        entry[QStringLiteral("memory_mb")] = 7.0;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QCOMPARE(cells.cpu, QStringLiteral("5.0"));
        QCOMPARE(cells.memory, QStringLiteral("2.0"));
    }

    void halfAMeasurementStillCounts()
    {
        // A runtime that reports memory and no CPU has measured something, and
        // the row should show what there is rather than nothing.
        QVariantMap entry;
        entry[QStringLiteral("memory_mb")] = 4.0;

        const ModuleStatsCells cells = moduleStatsCells(entry);

        QVERIFY(cells.measured);
        QCOMPARE(cells.memory, QStringLiteral("4.0"));
        QVERIFY(cells.cpu.isEmpty());
    }

    void aNonNumericFigureIsNotAMeasurement()
    {
        QVariantMap entry;
        entry[QStringLiteral("memory_mb")] = QStringLiteral("plenty");

        QVERIFY(!moduleStatsCells(entry).measured);
    }
};

QTEST_MAIN(ModuleStatsCellsTest)
#include "module_stats_cells_test.moc"
