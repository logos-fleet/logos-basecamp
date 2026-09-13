#include "InstallGate.h"

namespace basecamp::appmanager {

InstallGate::InstallGate(Modules* modules)
    : m_modules(modules)
{
}

void InstallGate::refuse(const QString& why)
{
    m_stage = Stage::Refused;
    m_error = why;
    m_prompt.clear();
}

bool InstallGate::begin(const CatalogEntry& entry)
{
    m_stage = Stage::Idle;
    m_error.clear();
    m_prompt.clear();
    m_lgxPath.clear();
    m_installedPath.clear();
    m_entry = entry;

    if (!m_modules) {
        refuse(QStringLiteral("this shell has no package modules"));
        return false;
    }

    // Step 1, and it asks nothing: an unavailable row has no install control, so
    // reaching here with one is a wiring bug, not a user action. Refusing before
    // any network traffic is what keeps that bug from becoming a download.
    if (!entry.available) {
        refuse(entry.unavailableReason.isEmpty()
                   ? QStringLiteral("'%1' cannot be installed in this build").arg(entry.name)
                   : entry.unavailableReason);
        return false;
    }
    if (entry.installed) {
        refuse(QStringLiteral("'%1' is already installed at %2")
                   .arg(entry.name, entry.installedVersion));
        return false;
    }

    // Step 2. The catalog's advertised signer is a claim about bytes that have
    // not arrived, which is why the prompt cannot come first.
    const QVariantMap downloaded =
        m_modules->downloadPinned(entry.repositoryUrl, entry.name, entry.version);
    // SUCCESS IS THE ABSENCE OF `error`, which is package_downloader's contract
    // and is easy to get backwards -- the same shape installPlugin has below.
    // There is no `success` key in the answer: reading one made every download
    // that had just WORKED read as a failure, because an absent key defaults to
    // false. Measured on an iPad simulator, 2026-09-13: a 1.3 MB package
    // downloaded, passed the index binding, and was refused one line later.
    const QString err = downloaded.value(QStringLiteral("error")).toString();
    if (!err.isEmpty()) {
        refuse(err);
        return false;
    }
    m_lgxPath = downloaded.value(QStringLiteral("path")).toString();
    if (m_lgxPath.isEmpty()) {
        // A success with no path is not a success. Installing "" would ask
        // package_manager to read a directory and report a confusing error two
        // steps from the cause.
        refuse(QStringLiteral("the download of '%1' reported success but no file").arg(entry.name));
        return false;
    }

    // Step 3. package_manager owns the policy; the gate does not second-guess
    // `installable`, it just refuses to prompt about something that cannot be
    // installed. Showing a signer beside an Install button that will fail is the
    // one thing signerTrust exists to prevent.
    m_prompt = m_modules->signerTrust(m_lgxPath);
    if (!m_prompt.value(QStringLiteral("installable"), false).toBool()) {
        const QString why = m_prompt.value(QStringLiteral("reason")).toString();
        refuse(why.isEmpty()
                   ? QStringLiteral("'%1' cannot be installed as signed").arg(entry.name)
                   : why);
        return false;
    }

    // Step 4: stop, and wait for a person.
    m_stage = Stage::AwaitingSigner;
    return true;
}

bool InstallGate::approve()
{
    if (m_stage != Stage::AwaitingSigner) {
        refuse(QStringLiteral("nothing is waiting for a signer decision"));
        return false;
    }

    // Step 5. The same gate runs again inside installPlugin — signerTrust is
    // advice about what WILL happen and the installer is what happens — so a
    // package that changed on disk between the prompt and the approval is
    // refused here rather than installed on the strength of a stale verdict.
    const QVariantMap result = m_modules->installPlugin(m_lgxPath);
    if (result.contains(QStringLiteral("error"))) {
        refuse(result.value(QStringLiteral("error")).toString());
        return false;
    }

    m_installedPath = result.value(QStringLiteral("path")).toString();
    m_stage = Stage::Installed;
    m_error.clear();
    m_prompt.clear();
    return true;
}

void InstallGate::reject(const QString& why)
{
    if (m_stage != Stage::AwaitingSigner) {
        refuse(QStringLiteral("nothing is waiting for a signer decision"));
        return;
    }
    refuse(why.isEmpty()
               ? QStringLiteral("you did not trust the signer of '%1'").arg(m_entry.name)
               : why);
}

} // namespace basecamp::appmanager
