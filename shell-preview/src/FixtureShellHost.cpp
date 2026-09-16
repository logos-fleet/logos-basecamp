#include "FixtureShellHost.h"

#include <QDebug>

FixtureShellHost::FixtureShellHost(const QJsonObject& fixture)
    : m_backend(fixture) {}

QObject* FixtureShellHost::backendObject() { return &m_backend; }

int  FixtureShellHost::currentSectionIndex() const { return m_backend.currentActiveSectionIndex(); }

void FixtureShellHost::setCurrentSectionIndex(int index)
{
    m_backend.setCurrentActiveSectionIndex(index);
    if (m_observer) m_observer->onSectionIndexChanged(index);
}

void FixtureShellHost::loadUiModule(const QString& name)
{
    // THE PREVIEW HOST REFUSES EVERY MOUNT, and now it says so (#205).
    //
    // Nothing here loads a plugin -- PluginLoader is host-side and reinstating
    // it would pull in the whole runtime this binary exists to avoid -- so
    // pressing an app tile in the preview used to do nothing at all: no window,
    // no message, exactly the silence a phone gave an app whose module was not
    // on the device. The shell has a surface for this now, so the preview uses
    // the real one rather than a log line.
    const QString why = QStringLiteral("the preview host loads no plugins, so %1's "
                                       "view cannot be instantiated here").arg(name);
    qInfo().noquote() << "FixtureShellHost: loadUiModule" << name << "--" << why;
    if (m_observer) m_observer->onUiModuleUnavailable(name, why);
}

void FixtureShellHost::unloadUiModule(const QString& name)
{
    qInfo() << "FixtureShellHost: unloadUiModule" << name;
}

void FixtureShellHost::setCurrentVisibleApp(const QString& name)
{
    m_backend.setCurrentVisibleApp(name);
}

QString FixtureShellHost::displayNameFor(const QString& name) const
{
    return m_backend.displayNameFor(name);
}

void FixtureShellHost::setObserver(IShellObserver* observer) { m_observer = observer; }
