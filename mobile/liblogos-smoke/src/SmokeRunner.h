// Brings liblogos_core up inside the app sandbox and reports what it sees.
// No modules are installed and no capability_module is loaded: this is the
// first moment Logos runtime code runs on a phone, and what it has to prove
// is that the core starts, finds its directories, and answers.
#pragma once

#include <QObject>
#include <QString>

class SmokeRunner : public QObject
{
    Q_OBJECT
public:
    explicit SmokeRunner(QObject* parent = nullptr);

    // Blocks for the duration of logos_core_start(); returns elapsed ms.
    qint64 run(int argc, char* argv[]);

    // Shuts the core down (logos_core_cleanup). Guarded on m_started, because
    // cleanup on a core that never started is not defined by the C API.
    void stop();

signals:
    void log(const QString& line);

private:
    bool m_started = false;
};
