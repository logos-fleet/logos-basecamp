#include "ModuleStatsCells.h"

#include <QStringList>

namespace basecamp {

namespace {

// The first of `keys` the entry reports as a number, formatted. Empty when it
// reports none of them.
//
// PRESENCE, NOT MAGNITUDE, is the test — and that is the fix. The rule this
// replaces took "the first NON-ZERO hit", which means a module that really was
// idle fell straight through every spelling and ended up at zero by accident
// rather than by measurement. It also meant a runtime spelling it `cpu_percent`
// was indistinguishable from one reporting nothing.
QString figure(const QVariantMap& entry, const QStringList& keys)
{
    for (const QString& key : keys) {
        const QVariant value = entry.value(key);
        // An absent key gives an invalid QVariant and a null one gives a valid
        // but null QVariant; neither is a reading. `toDouble(&ok)` is what
        // rejects a string the runtime could not turn into a number.
        if (!value.isValid() || value.isNull())
            continue;
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok)
            continue;
        return QString::number(number, 'f', 1);
    }
    return {};
}

} // namespace

ModuleStatsCells moduleStatsCells(const QVariantMap& entry)
{
    ModuleStatsCells cells;
    // The facade's name first: when both are present they are the same number,
    // and preferring it keeps the host that renamed them authoritative.
    cells.cpu = figure(entry,
                       {QStringLiteral("cpuPercent"),
                        QStringLiteral("cpu_percent"),
                        QStringLiteral("cpu")});
    cells.memory = figure(entry,
                          {QStringLiteral("memoryMb"),
                           QStringLiteral("memory_mb"),
                           QStringLiteral("memory"),
                           QStringLiteral("memory_MB")});
    // A reported figure is never the empty string — QString::number gives at
    // least "0.0" — so an empty cell is exactly a figure nobody reported, and
    // one of the two is enough to call the row measured.
    cells.measured = !cells.cpu.isEmpty() || !cells.memory.isEmpty();
    return cells;
}

} // namespace basecamp
