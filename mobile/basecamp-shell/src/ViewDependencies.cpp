#include "ViewDependencies.h"

namespace basecamp::shell {

QStringList declaredDependencies(const ModuleFacts& facts, const QString& name)
{
    for (const QVariant& value : facts.bundledSet) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("name")).toString() != name)
            continue;
        QStringList deps;
        // The manifest writes them as a list of strings (nix/verify-lgx-member.sh
        // flattens the `{ name = ...; }` spelling a catalog entry also allows),
        // so a QStringList is the shape; read through QVariantList all the same,
        // because a QVariant carrying either must answer the same here.
        for (const QVariant& dep : entry.value(QStringLiteral("dependencies")).toList()) {
            const QString spelled = dep.toString();
            if (!spelled.isEmpty() && !deps.contains(spelled))
                deps << spelled;
        }
        return deps;
    }
    return { };
}

ViewMountVerdict bringUpViewDependencies(const ModuleFacts& facts, const QString& name,
                                         const std::function<bool(const QString&)>& load)
{
    ViewMountVerdict verdict;
    for (const QString& dep : declaredDependencies(facts, name)) {
        if (facts.loaded.contains(dep))
            continue;
        if (!facts.known.contains(dep)) {
            // NOT offered to the loader. There is nothing on this device to
            // load, and asking the core would report a failure that means
            // something else -- "the image is broken" rather than "the image is
            // not here", which is the one distinction the person reading the
            // screen can act on.
            verdict.missing << dep;
            continue;
        }
        if (!load(dep)) {
            verdict.refused << dep;
            continue;
        }
        verdict.loaded << dep;
    }
    // EVERY declared dependency is walked before the verdict, rather than
    // stopping at the first bad one: a view that names three missing modules
    // should say so once, not three mounts in a row.
    verdict.ready = verdict.missing.isEmpty() && verdict.refused.isEmpty();
    return verdict;
}

} // namespace basecamp::shell
