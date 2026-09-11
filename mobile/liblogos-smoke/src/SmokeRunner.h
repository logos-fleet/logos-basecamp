// Brings liblogos_core up inside the app sandbox and reports what it sees.
// No modules are installed and none are loaded YET -- the Bundled set is
// BundledSetRunner's business: this is the first moment Logos runtime code
// runs on a phone, and what it has to prove is that the core starts, finds its
// directories, and answers.
#pragma once

#include "ICoreRuntime.h"

#include <QObject>
#include <QString>

class SmokeRunner : public QObject
{
    Q_OBJECT
public:
    explicit SmokeRunner(QObject* parent = nullptr);

    // Everything that has to happen BEFORE the runtime starts -- transport
    // mode, the sandbox directories, logos_core_init -- and the Config those
    // directories make up. Starting the core is BundledSetCoreRuntime's job:
    // this host has an ICoreRuntime and must not reach past it.
    ICoreRuntime::Config prepare(int argc, char* argv[]);

    // What the core sees once it is up. Separate from prepare() because it
    // describes the started core, and nothing here starts one.
    void report();

    // Shuts the core down (logos_core_cleanup). Guarded on m_prepared, because
    // cleanup on a core that never started is not defined by the C API.
    void stop();

signals:
    void log(const QString& line);

private:
    bool m_prepared = false;
};
