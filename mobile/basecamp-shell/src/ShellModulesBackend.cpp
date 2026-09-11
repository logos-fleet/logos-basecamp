#include "ShellModulesBackend.h"

#include "BundledSetCoreRuntime.h"
#include "CoreModuleManager.h"
#include "ShellSections.h"

#include <logos_api.h>

namespace {

// The caller name this shell introspects modules under. Not "basecamp": the
// desktop app already uses that on the same protocol, and a module's access
// policy is written against the name that calls it.
const char* kApiName = "basecamp_shell";

} // namespace

ShellModulesBackend::ShellModulesBackend(BundledSetCoreRuntime* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_api(new LogosAPI(QString::fromUtf8(kApiName)))
    , m_modules(new CoreModuleManager(m_api, core, this))
    , m_coreModulesModel(new ModuleInstanceModel(this))
{
    // The same wiring MainUIBackend has: the manager announces "the module set
    // changed" -- on refresh() and on every 2s stats tick -- and the model is
    // rebuilt from it. ModuleInstanceModel patches in place when the names are
    // unchanged, so the tick moves cpu/memory and nothing else.
    connect(m_modules, &CoreModuleManager::coreModulesChanged,
            this, &ShellModulesBackend::rebuildRows);
    rebuildRows();
}

ShellModulesBackend::~ShellModulesBackend()
{
    // m_modules is a QObject child and goes with this object; m_api is not,
    // and is deleted last because the manager's introspection calls reach
    // through it.
    delete m_modules;
    m_modules = nullptr;
    delete m_api;
}

QAbstractItemModel* ShellModulesBackend::coreModulesModel() const
{
    return m_coreModulesModel;
}

QString ShellModulesBackend::buildVersion() const
{
    return QStringLiteral("mobile");
}

bool ShellModulesBackend::isHostLoaded(const QString& name) const
{
    for (const QVariant& row : m_core->bundledSet()) {
        const QVariantMap entry = row.toMap();
        if (entry.value(QStringLiteral("name")).toString() == name)
            return entry.value(QStringLiteral("type")).toString() == QLatin1String("ui_qml");
    }
    return false;
}

QVariantList ShellModulesBackend::snapshot() const
{
    // The Bundled set is the row list, in the manifest's order -- which is the
    // closure's load order, dependencies first. Deliberately NOT
    // knownModules(): on a phone the app's installed set is what the build
    // embedded, and a member the core refused to register is exactly the thing
    // the Modules tab has to keep showing (as "Not loaded") rather than hide.
    const QStringList loaded = m_modules->loadedModules();
    const QStringList known  = m_modules->knownModules();

    QVariantList rows;
    for (const QVariant& value : m_core->bundledSet()) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString();
        const bool hostLoaded = type == QLatin1String("ui_qml");

        QVariantMap row;
        row[QStringLiteral("name")] = name;
        row[QStringLiteral("displayName")] = name;
        row[QStringLiteral("version")] = entry.value(QStringLiteral("version"));
        row[QStringLiteral("type")] = type;
        row[QStringLiteral("category")] = QStringLiteral("bundled");
        // Every member came in with the app image, which is what "embedded"
        // means everywhere else in Basecamp: not installed by the user, and
        // not removable.
        row[QStringLiteral("installType")] = QStringLiteral("embedded");
        row[QStringLiteral("isLoaded")] = hostLoaded || loaded.contains(name);
        // A Bundled member the core never registered has something wrong with
        // its image -- the closure was resolved and verified at build time, so
        // there is no missing dependency to install. Saying so in the one
        // status column the row has beats a silent "Not loaded".
        row[QStringLiteral("hasMissingDeps")] = !hostLoaded && !known.contains(name);

        const QVariantMap stats = m_modules->moduleStats(name);
        row[QStringLiteral("cpu")] = stats.value(QStringLiteral("cpu"), 0.0);
        row[QStringLiteral("memory")] = stats.value(QStringLiteral("memory"), 0.0);
        rows.append(row);
    }
    return rows;
}

void ShellModulesBackend::rebuildRows()
{
    m_coreModulesModel->replaceRows(snapshot());
}

QStringList ShellModulesBackend::moduleRowNames() const
{
    QStringList names;
    for (int i = 0; i < m_coreModulesModel->rowCount(); ++i)
        names << m_coreModulesModel->data(m_coreModulesModel->index(i, 0),
                                          ModuleInstanceRoles::NameRole).toString();
    return names;
}

QStringList ShellModulesBackend::bundledSetNames() const
{
    QStringList names;
    for (const QVariant& row : m_core->bundledSet())
        names << row.toMap().value(QStringLiteral("name")).toString();
    return names;
}

void ShellModulesBackend::refreshCoreModules()
{
    m_modules->refresh();
}

void ShellModulesBackend::loadCoreModule(const QString& moduleName)
{
    if (isHostLoaded(moduleName)) {
        // Not a refusal to be fixed: a view module's image is a Qt-backed
        // framework this process instantiates itself, so the core has no
        // handle on it to load (ADR 0006). It is in the set and in this list,
        // and its state is the host's.
        emit log(QStringLiteral("%1 is a view module: host-loaded, not the core's to load")
                     .arg(moduleName));
        return;
    }
    emit log(QStringLiteral("load %1").arg(moduleName));
    m_modules->loadModule(moduleName);
    rebuildRows();
}

void ShellModulesBackend::unloadCoreModule(const QString& moduleName)
{
    if (isHostLoaded(moduleName)) {
        emit log(QStringLiteral("%1 is a view module: host-loaded, not the core's to unload")
                     .arg(moduleName));
        return;
    }
    emit log(QStringLiteral("unload %1").arg(moduleName));
    // With dependents, as the desktop's cascade does: a Bundled set is a
    // resolved closure, so taking a member down under a loaded dependent is
    // the one shape of unload that leaves the process pointing at nothing.
    m_modules->unloadModuleWithDependents(moduleName);
    rebuildRows();
}

QString ShellModulesBackend::getCoreModuleMethods(const QString& moduleName)
{
    return m_modules->getMethods(moduleName);
}

QString ShellModulesBackend::getCoreModuleEvents(const QString& moduleName)
{
    return m_modules->getEvents(moduleName);
}

QString ShellModulesBackend::callCoreModuleMethod(const QString& moduleName,
                                                  const QString& methodName,
                                                  const QString& argsJson)
{
    return m_modules->callMethod(moduleName, methodName, argsJson);
}

void ShellModulesBackend::setCurrentActiveSectionIndex(int index)
{
    if (!ShellSection::isValid(index) || index == m_sectionIndex)
        return;
    m_sectionIndex = index;
    emit currentActiveSectionIndexChanged();
}

// ── inert ───────────────────────────────────────────────────────────────────
// One line each, and each one says what a phone does not have yet rather than
// returning quietly. The Shell renders these surfaces; a user who reaches one
// should get an answer, not a dead control.

namespace {
QString notHere(const char* what)
{
    return QStringLiteral("%1 is not available on this shell yet").arg(QString::fromUtf8(what));
}
} // namespace

void ShellModulesBackend::loadUiModule(const QString& n)   { emit log(notHere("UI plugins") + ": " + n); }
void ShellModulesBackend::unloadUiModule(const QString& n) { emit log(notHere("UI plugins") + ": " + n); }
void ShellModulesBackend::onAppLauncherClicked(const QString& n) { emit log(notHere("apps") + ": " + n); }
void ShellModulesBackend::setCurrentVisibleApp(const QString&)   { }

void ShellModulesBackend::uninstallApp(const QString& n, const QString&) { emit log(notHere("uninstall") + ": " + n); }
void ShellModulesBackend::confirmUnloadCascade(const QString&)           { emit log(notHere("the unload cascade")); }
void ShellModulesBackend::confirmUninstallCascade(const QString&)        { emit log(notHere("uninstall")); }
void ShellModulesBackend::confirmUninstallMultiCascade(const QStringList&) { emit log(notHere("uninstall")); }
void ShellModulesBackend::cancelMultiUninstall(const QStringList&)       { }
void ShellModulesBackend::cancelPendingAction(const QString&)            { }
void ShellModulesBackend::cancelPendingUninstallApp(const QString&)      { }
void ShellModulesBackend::confirmInstallGate(const QString&)             { emit log(notHere("install")); }
void ShellModulesBackend::cancelInstallGate(const QString&)              { }
void ShellModulesBackend::openApp(const QString& n, const QString&, const QVariantMap&, bool)
{
    emit log(notHere("apps") + ": " + n);
}
void ShellModulesBackend::confirmCatalogInstall(const QString&, const QString&, const QVariantMap&)
{
    emit log(notHere("catalog install"));
}
void ShellModulesBackend::notifyAddApplicationDialogClosed() { }
void ShellModulesBackend::respondToShellIntent(const QString&, bool, const QVariantMap&, const QString&)
{
    emit log(notHere("app-to-app intents"));
}
void ShellModulesBackend::refreshUiModules()   { }
void ShellModulesBackend::refreshRepositories() { emit repositoriesChanged(); }
void ShellModulesBackend::refreshAppCatalog()   { }
void ShellModulesBackend::addRepository(const QString&)        { emit log(notHere("repositories")); }
void ShellModulesBackend::removeRepository(const QString&)     { emit log(notHere("repositories")); }
void ShellModulesBackend::setRepositoryEnabled(const QString&, bool) { emit log(notHere("repositories")); }
