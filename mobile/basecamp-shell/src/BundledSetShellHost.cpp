#include "BundledSetShellHost.h"

#include "ShellSections.h"

BundledSetShellHost::BundledSetShellHost(BundledSetCoreRuntime* core)
    : m_backend(core)
{
    // The sidebar writes the section index straight into the backend; the
    // content stack only follows if the observer hears about it.
    QObject::connect(&m_backend, &ShellModulesBackend::currentActiveSectionIndexChanged,
                     &m_backend, [this] {
                         if (m_observer)
                             m_observer->onSectionIndexChanged(m_backend.currentActiveSectionIndex());
                     });
}

BundledSetShellHost::~BundledSetShellHost() = default;

QObject* BundledSetShellHost::backendObject() { return &m_backend; }

int BundledSetShellHost::currentSectionIndex() const
{
    return m_backend.currentActiveSectionIndex();
}

void BundledSetShellHost::setCurrentSectionIndex(int index)
{
    m_backend.setCurrentActiveSectionIndex(index);
}

void BundledSetShellHost::loadUiModule(const QString& name)   { m_backend.loadUiModule(name); }
void BundledSetShellHost::unloadUiModule(const QString& name) { m_backend.unloadUiModule(name); }
void BundledSetShellHost::setCurrentVisibleApp(const QString& name)
{
    m_backend.setCurrentVisibleApp(name);
}

QString BundledSetShellHost::displayNameFor(const QString& name) const { return name; }

void BundledSetShellHost::setObserver(IShellObserver* observer) { m_observer = observer; }

void BundledSetShellHost::replaySection()
{
    if (m_observer && m_backend.currentActiveSectionIndex() != ShellSection::Workspace)
        m_observer->onSectionIndexChanged(m_backend.currentActiveSectionIndex());
}
