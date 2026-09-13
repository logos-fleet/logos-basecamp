#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace basecamp::appmanager {

// A publisher this device will accept packages from, as the keyring records it.
//
// BOTH HALVES, because the keyring is keyed by `name` and checked against `did`:
// the name is a label the user can recognise in a prompt, the DID is the thing a
// signature is actually verified under, and an entry with only one of them is
// either unfindable or unverifiable.
struct TrustAnchor {
    QString name;
    QString did;
};

// WHAT A STORE SHELL WAS POINTED AT, parsed once from its own command line.
//
// A Store shell browses whatever repositories the device is configured with, and
// the default is a catalog on the internet. Pointing one at a LOCAL catalog
// release -- the one a developer serves off their own machine while building it
// -- is what the runtime-install path is exercised against, so it arrives the
// way `--bundle` does: as an argument, read once, at startup.
//
//     --repository <url>            a logos-repo.json to add
//     --trust-signer <name>=<did>   anchor a publisher in this device's keyring
//     --install <package>           install this row once the catalog is up
//
// THE TWO RULES HERE ARE NOT NEW ONES.
//
// A repository URL is fetched, and it comes from the same kind of author as a
// row's report link, so it goes through the SAME check -- CatalogEntry's
// `linkRefusal`. A second implementation of "which URLs will this shell touch"
// is a second answer, and the laxer of the two is the one that decides.
//
// A trust anchor that is not a DID trusts NOBODY while looking like it trusted
// somebody: `lgx keyring` would take the string, no signature would ever verify
// under it, and every install would then refuse under the `require` policy with
// a message about the package rather than about the keyring. It is refused here,
// where the reason is still legible.
//
// EVERY REFUSAL IS KEPT, not dropped. A Store shell's whole diagnostic surface is
// one console; a mistyped flag that silently does nothing is indistinguishable
// from a catalog that is down.
class CatalogSource {
public:
    // Parse an argument list -- QCoreApplication::arguments(), including the
    // program name. Anything unrecognised belongs to Qt or to the platform and
    // is left alone.
    static CatalogSource fromArguments(const QStringList& args);

    // The repository to add, or empty when none was asked for or the one asked
    // for was refused.
    QString repositoryUrl() const { return m_repositoryUrl; }

    // The publishers to anchor, in the order they were given.
    QList<TrustAnchor> trustAnchors() const { return m_anchors; }

    // The catalog row to install once the catalog is up, or empty.
    QString installPackage() const { return m_install; }

    // One sentence per refused argument, in the order they were met.
    QStringList refusals() const { return m_refusals; }

    // Nothing was asked for at all -- an ordinary launch. A source holding a
    // REFUSAL is not empty: the user asked for something and must be told it
    // did not happen.
    bool isEmpty() const;

private:
    QString            m_repositoryUrl;
    QList<TrustAnchor> m_anchors;
    QString            m_install;
    QStringList        m_refusals;
};

} // namespace basecamp::appmanager
