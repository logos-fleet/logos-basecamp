#ifndef LOGOS_BASECAMP_MODULESTATSCELLS_H
#define LOGOS_BASECAMP_MODULESTATSCELLS_H

#include <QString>
#include <QVariantMap>

namespace basecamp {

// THE TWO FIGURES A MODULES-TAB ROW SHOWS, read out of ONE entry of
// ICoreRuntime::allStats().
//
// The runtime under a Basecamp host is not one thing, and the entries do not
// all spell the same figure the same way:
//
//   cpuPercent / memoryMb     the SDK facade's names (logos_host_core.h),
//                             which is what the desktop host sees;
//   cpu_percent / memory_mb   the CORE's own names — what a phone's Shell hands
//                             through untouched, since it parses liblogos' JSON
//                             rather than going through the facade;
//   cpu / memory / memory_MB  older hosts.
//
// Reading only the first pair is how every Bundled module on a phone came to
// show 0.0% / 0.0 MB whatever it was doing (#86): the keys the runtime actually
// emitted were never looked at, and a missing key reads as zero.
//
// A MISSING MEASUREMENT IS NOT A MEASUREMENT OF ZERO, which is the other half.
// liblogos reports null for a module nothing could account for, deliberately
// rather than 0, so that "never measured" and "measured, idle" stay different
// answers — and a row that renders the first as "0.0 MB" is making a claim
// nobody made. `measured` is what carries that distinction to the view.
struct ModuleStatsCells {
    // Formatted to one decimal, as the QML-facing contract has always been.
    // EMPTY when this figure was not reported — not "0.0".
    QString cpu;
    QString memory;
    // False when neither figure was reported. The row then shows the same em
    // dash an unloaded row gets, because that is what it is: no reading.
    bool measured = false;
};

ModuleStatsCells moduleStatsCells(const QVariantMap& entry);

} // namespace basecamp

#endif // LOGOS_BASECAMP_MODULESTATSCELLS_H
