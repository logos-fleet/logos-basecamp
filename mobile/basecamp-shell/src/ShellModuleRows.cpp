#include "ShellModuleRows.h"

namespace basecamp::shell {

namespace {

const QLatin1String kCoreModuleType("core");

// The rule `isWebContainerApp` publishes, taking the manifest's name list from
// the caller: `launcherApps` asks it once per known module and must not rebuild
// that list every time.
bool runsInWebContainer(const ModuleFacts& facts, const QStringList& bundled,
                        const QString& name)
{
    // EITHER EVIDENCE, and they answer the same question from two sides: the
    // container has a UI page open for it right now, or its installed package
    // says it has one (#123). The second is what a module that is installed and
    // not running has -- which on every launch after the one that installed it
    // is every Downloaded module, because a Store shell's cold start does not
    // load what it discovers. Pressing the tile is what brings it up.
    return (facts.openPages.contains(name) || facts.uiPackages.contains(name))
        && (facts.shipped.contains(name) || !bundled.contains(name));
}

QVariantMap tile(const QString& name, bool loaded)
{
    QVariantMap app;
    app[QStringLiteral("name")] = name;
    // Neither half carries a display name here. A Bundled manifest does not
    // reproduce the catalog entry's, and a Downloaded module is known to the
    // Shell by the name the core discovered it under. The delegate falls back
    // to it anyway.
    app[QStringLiteral("displayName")] = name;
    app[QStringLiteral("isLoaded")] = loaded;
    // No icon travels with either: an embedded framework has nowhere in
    // <App>.app/Frameworks/ to put one beside the image, and an installed `web`
    // package's icon is an LGX asset the container does not serve. The delegate
    // draws the initial instead.
    app[QStringLiteral("iconPath")] = QString();
    app[QStringLiteral("supportsFullBleedIcon")] = false;
    app[QStringLiteral("hasMissingDeps")] = false;
    app[QStringLiteral("depBlockKind")] = QString();
    return app;
}

} // namespace

QStringList bundledNames(const QVariantList& bundledSet)
{
    QStringList names;
    for (const QVariant& row : bundledSet)
        names << row.toMap().value(QStringLiteral("name")).toString();
    return names;
}

QStringList downloadedModules(const ModuleFacts& facts)
{
    const QStringList bundled = bundledNames(facts.bundledSet);
    QStringList out;
    for (const QString& name : facts.known) {
        // Every Bundled member the core registered is in `known` too; counting
        // one here would put a second row under the same name. So is every
        // module out of the app's own `web-modules` tree -- discovered exactly
        // as an installed one is, and shipped all the same.
        if (!bundled.contains(name) && !facts.shipped.contains(name))
            out << name;
    }
    return out;
}

namespace {

// A module the core DISCOVERED rather than one the manifest declared: it has
// no version, no declared type and no dependency story the Shell can read --
// only a name, whether it is loaded, and whether the Web container opened a
// page for it.
QVariantMap discoveredRow(const ModuleFacts& facts, const QString& name, bool embedded)
{
    QVariantMap row;
    row[QStringLiteral("name")] = name;
    row[QStringLiteral("displayName")] = name;
    // The core discovers a module, not a catalog entry: the version the package
    // advertised is the App Manager's to show, and inventing one here would be
    // a second answer to it.
    row[QStringLiteral("version")] = QString();
    // Its page, or its package's own manifest when nothing has opened one yet
    // -- the same pair of answers the tile is made from, so a row cannot call
    // a module a `core` one while the sidebar carries an app tile for it.
    row[QStringLiteral("type")] =
        (facts.openPages.contains(name) || facts.uiPackages.contains(name))
            ? kViewModuleType : kCoreModuleType;
    row[QStringLiteral("category")] =
        embedded ? QStringLiteral("bundled") : QStringLiteral("downloaded");
    row[QStringLiteral("installType")] =
        embedded ? QStringLiteral("embedded") : QStringLiteral("downloaded");
    // Unlike a Bundled VIEW module, this one is the CORE's whatever its type:
    // its page lives in the Web container, which is a core container.
    row[QStringLiteral("isLoaded")] = facts.loaded.contains(name);
    // It is in this list BECAUSE the core discovered it, so there is nothing
    // here that a missing dependency could mean.
    row[QStringLiteral("hasMissingDeps")] = false;
    return row;
}

} // namespace

QVariantList moduleRows(const ModuleFacts& facts)
{
    QVariantList rows;

    for (const QVariant& value : facts.bundledSet) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString();
        const bool isView = type == kViewModuleType;

        QVariantMap row;
        row[QStringLiteral("name")] = name;
        row[QStringLiteral("displayName")] = name;
        row[QStringLiteral("version")] = entry.value(QStringLiteral("version"));
        row[QStringLiteral("type")] = type;
        row[QStringLiteral("category")] = QStringLiteral("bundled");
        // Every member came in with the app image, which is what "embedded"
        // means everywhere else in Basecamp: not installed by the user, and not
        // removable.
        row[QStringLiteral("installType")] = QStringLiteral("embedded");
        // A view module reads as loaded when the HOST has it mounted, not
        // because it is one: the whole claim of the Modules tab is that it
        // shows what is running.
        row[QStringLiteral("isLoaded")] =
            isView ? facts.mountedViews.contains(name) : facts.loaded.contains(name);
        // A Bundled member the core never registered has something wrong with
        // its image -- the closure was resolved and verified at build time, so
        // there is no missing dependency to install. Saying so in the one
        // status column the row has beats a silent "Not loaded".
        row[QStringLiteral("hasMissingDeps")] = !isView && !facts.known.contains(name);
        rows.append(row);
    }

    // The app's own `web-modules` tree, in the order the core discovered it.
    // After the manifest, because the manifest's order is the closure's LOAD
    // order and these are not in that closure; before the Downloaded ones,
    // because they came with the build.
    for (const QString& name : facts.known) {
        if (facts.shipped.contains(name))
            rows.append(discoveredRow(facts, name, /*embedded=*/true));
    }
    for (const QString& name : downloadedModules(facts))
        rows.append(discoveredRow(facts, name, /*embedded=*/false));

    return rows;
}

QVariantList launcherApps(const ModuleFacts& facts)
{
    QVariantList apps;
    for (const QVariant& value : facts.bundledSet) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("type")).toString() != kViewModuleType)
            continue;
        const QString name = entry.value(QStringLiteral("name")).toString();
        apps.append(tile(name, facts.mountedViews.contains(name)));
    }
    // A page -- or a package that declares one -- is what makes one an app, and
    // that is the same question for a shipped `web` module as for an installed
    // one: neither is in the manifest, and both run in the Web container.
    const QStringList bundled = bundledNames(facts.bundledSet);
    for (const QString& name : facts.known) {
        if (runsInWebContainer(facts, bundled, name))
            apps.append(tile(name, facts.loaded.contains(name)));
    }
    return apps;
}

bool isWebContainerApp(const ModuleFacts& facts, const QString& name)
{
    return runsInWebContainer(facts, bundledNames(facts.bundledSet), name);
}

} // namespace basecamp::shell
