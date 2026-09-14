#pragma once

#include "CatalogEntry.h"

#include <QString>
#include <QVariantMap>

namespace basecamp::appmanager {

// THE ORDER AN APP MANAGER INSTALLS IN, AND THE THREE PLACES IT STOPS.
//
// Installing a Downloaded module from a catalog is five steps, and four of them
// can refuse:
//
//   1. the row must be installable HERE      — an unavailable row never starts
//   2. download the package                  — network, checksum, root hash
//   3. ask package_manager who signed it     — name, DID, keyring, policy
//   4. SHOW THE USER and wait               — guideline 15.x / the LGX signing
//                                             design: nothing is installed
//                                             behind this
//   5. install                               — package_manager, same gate again
//
// The ORDER is the thing worth testing, and it is what this class is. Two
// orderings are wrong in ways that pass a casual reading: installing and then
// showing the signer (a prompt after the fact is not consent), and prompting
// before the download (there is no signer to show — a catalog's advertised DID
// is a claim about bytes that have not arrived).
//
// EVERYTHING THAT TOUCHES A MODULE IS BEHIND `Modules`. That is what makes the
// order testable on a desktop with no core, no network and no catalog: a test
// supplies three answers and asserts what was asked, in which order, and what
// the gate did with each. The Shell's implementation of that interface is the
// only part that needs a running phone.
//
// IT DOES NOT LOAD THE MODULE. A module installed into the Web container is
// loaded by the core when the Shell asks, and a gate that also started it would
// own two failures that want different messages ("it did not install" and "it
// installed and will not run").
class InstallGate {
public:
    // The two modules this flow drives, and nothing else. Each returns the
    // module's own LogosMap, verbatim — the gate reads the keys those methods
    // document rather than a translation, so a change to either shows up here
    // and not in a mapping layer nobody reads.
    class Modules {
    public:
        virtual ~Modules() = default;

        // package_downloader.downloadPinned. Returns { name, path, ... } or
        // { name, error }. Success is the ABSENCE of `error` -- there is no
        // `success` key, and reading one reads every successful download as a
        // failure.
        virtual QVariantMap downloadPinned(const QString& repositoryUrl,
                                           const QString& packageName,
                                           const QString& version) = 0;

        // package_manager.signerTrust. Returns the signer prompt's contents
        // plus `installable` and `reason`.
        virtual QVariantMap signerTrust(const QString& lgxPath) = 0;

        // package_manager.installPlugin. Success is the ABSENCE of `error`,
        // which is that method's documented contract and is easy to get
        // backwards.
        virtual QVariantMap installPlugin(const QString& lgxPath) = 0;
    };

    enum class Stage {
        Idle,            // nothing in flight
        AwaitingSigner,  // downloaded; the user is looking at the signer prompt
        Installed,       // done
        Refused,         // the gate said no, or the user did. `error()` says which
    };

    explicit InstallGate(Modules* modules);

    // Start an install for one catalog row. Refuses without asking anything when
    // the row may not be offered here — the control should not have existed, and
    // a gate that trusted its caller would install a native-only package on a
    // phone if one button was ever mis-wired.
    //
    // Returns true when the flow reached the signer prompt.
    bool begin(const CatalogEntry& entry);

    Stage stage() const { return m_stage; }
    QString error() const { return m_error; }

    // What the prompt shows. Empty unless stage() is AwaitingSigner.
    //
    // `signerName` and `signerDid` are the two fields the criterion names; they
    // are shown TOGETHER because a name is self-asserted (the publisher chose
    // it) and the DID is what the keyring is checked against. A prompt with only
    // the name tells the user nothing they can verify.
    QVariantMap signerPrompt() const { return m_prompt; }

    // WHO WAS REFUSED, when the refusal came from the signer step and the
    // package carried a signer at all. Empty otherwise -- including for an
    // unsigned package, which has no DID, and for a refusal that happened
    // before signerTrust was ever asked.
    //
    // This exists because of the Store shell's `require` policy (ADR 0008). The
    // ONLY way forward from "signed by a key your keyring does not vouch for"
    // is for that DID to enter this device's keyring, and the DID is the thing
    // the user would have to be shown to get there. `signerPrompt()` cannot
    // carry it: that map is the Install-or-Cancel dialog, and putting an
    // un-installable package behind it is the one thing signerTrust exists to
    // prevent. So the refusal keeps the IDENTITY and drops the AFFORDANCE.
    //
    // Keys: name, version, signatureStatus, signerName, signerDid -- the
    // package_manager answer's identity half, verbatim, minus the verdict.
    QVariantMap refusedSigner() const { return m_refusedSigner; }

    // The user trusts this signer: install. Returns true when the package is
    // installed. Refuses if nothing is awaiting a decision.
    bool approve();

    // The user does not: nothing is installed and the downloaded file is not
    // touched again.
    void reject(const QString& why = {});

    // The package that was installed, once stage() is Installed.
    QString installedPath() const { return m_installedPath; }

private:
    void refuse(const QString& why);

    Modules* m_modules;   // not owned
    Stage    m_stage = Stage::Idle;
    QString  m_error;
    QString  m_lgxPath;
    QString  m_installedPath;
    QVariantMap m_prompt;
    QVariantMap m_refusedSigner;
    CatalogEntry m_entry;
};

} // namespace basecamp::appmanager
