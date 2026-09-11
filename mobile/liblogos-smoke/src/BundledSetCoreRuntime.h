// ICoreRuntime, answered from the Bundled-set manifest.
//
// Basecamp codes against ICoreRuntime (app/interfaces/ICoreRuntime.h) and has
// two implementations already: QtLogosCoreRuntime over a modules DIRECTORY, and
// FixtureCoreRuntime over a JSON fixture. On a Store shell neither is the
// answer. There is no modules directory to scan -- ADR 0003: a phone may not
// download native code, so the set is fixed at build time -- and the images sit
// in <App>.app/Frameworks/ or the native library directory, both flat and
// read-only.
//
// So this is the third one: the "installed set" is the manifest
// nix/bundled-set.nix wrote and the build compiled in (BundledSetManifest.h),
// and every member is registered with the core as a Bare module at start().
// Everything after that -- known, loaded, load, unload, refresh, stats -- is
// the core's own answer, exactly as on the desktop. What changes on a phone is
// where the set comes FROM, not what the shell may ask about it.
#pragma once

#include "ICoreRuntime.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

class BundledSetCoreRuntime : public QObject, public ICoreRuntime
{
    Q_OBJECT
public:
    // `manifestJson` defaults to the set compiled into this build, and
    // `imageDir` to the directory the running Logos image was loaded from.
    // Both are injectable so the runtime is testable off a fixture.
    explicit BundledSetCoreRuntime(const ICoreRuntime::Config& config,
                                   QObject* parent = nullptr,
                                   QString manifestJson = {},
                                   QString imageDir = {});
    ~BundledSetCoreRuntime() override;

    // Applies the Config, starts liblogos_core, then registers every non-view
    // member of the Bundled set. Registration is NOT loading: what a Store
    // shell ships and what it is running are different questions, and the
    // Modules tab shows both.
    void start() override;

    QStringList knownModules() const override;
    QStringList loadedModules() const override;
    bool loadModule(const QString& name,
                    LoadPolicy policy = LoadPolicy::RequiredDeps) override;
    bool unloadModule(const QString& name, bool withDependents = false) override;
    // A no-op that re-registers anything the core does not know yet. A Bundled
    // set cannot gain a member at runtime -- that is the whole point of it --
    // so there is nothing to re-scan; the method exists because the shell calls
    // it after an install, and on this shell an install adds a WEB module.
    void refreshModules() override;
    QVariantList allStats() const override;

    // The set as shipped, whether loaded or not: one entry per member, with
    // "name", "version", "type" and "image". This is what a Modules tab lists.
    QVariantList bundledSet() const;

    // Where the images were looked for. Public so a failure can be reported
    // with the path that was tried.
    static QString imageDirFromRunningImage();

    // Milliseconds logos_core_start() took, measured inside start().
    qint64 startMs() const { return m_startMs; }

signals:
    void log(const QString& line);

private:
    void registerBundledSet();
    // The bundle-relative image path of a member, or empty if the set has no
    // such member.
    QString imagePathOf(const QString& name) const;

    ICoreRuntime::Config m_config;
    QString m_imageDir;
    QJsonObject m_set;
    // Why m_set is empty, when it is empty because the manifest would not
    // parse. Reported from start(), the first point at which anyone can be
    // listening to log().
    QString m_manifestError;
    QStringList m_registered;
    qint64 m_startMs = -1;
    bool m_started = false;
};
