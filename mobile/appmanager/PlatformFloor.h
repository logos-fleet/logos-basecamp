#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace basecamp::appmanager {

// THIS SHELL'S PLATFORM FLOOR: which Platform modules it ships, and which ones
// the catalog it offers knows about and it does not.
//
// ADR 0009's second rule. A Downloaded module may depend on a Platform module
// -- one that owns access a webview cannot give it: a socket, a keystore, a
// radio (`"platform": true` in metadata.json) -- only where that module is in
// the Bundled set of the shell it lands on. A Bundled set is fixed at build
// time (ADR 0007), so on a shell that did not bundle it there is no later
// remedy: the install succeeds and the module dies at its first call.
//
// DERIVED, NEVER MAINTAINED. Nothing here decides which modules are Platform
// modules or which are bundled. nix/platform-floor.nix answers both off the
// mobile catalog index and the Bundled closure the shell was built with, and
// nix/bundled-set.nix writes the answer into bundled-set.json, which the build
// compiles into the app (BundledSetManifest.h). A hand-kept list would be a
// third home for a truth that already lives in metadata.json and in the shell's
// bundle set, and it drifts silently into the one failure ADR 0007's
// honest-availability rule exists to prevent.
//
// WHY THE WALK IS TRANSITIVE. `chat_module` is not a Platform module: it is
// built ON one -- its network comes from `delivery_module` -- and neither is
// `chat_ui`, which reaches `delivery_module` two edges down. A floor that read
// only a row's own flag would offer chat_ui on a shell that cannot deliver a
// single message. So the question asked of a row is "does anything you reach
// need a Platform module this build does not have", and the answer names that
// module rather than the row's own dependency.
class PlatformFloor {
public:
    PlatformFloor() = default;
    PlatformFloor(QStringList present, QStringList absent);

    // The floor out of a Bundled-set manifest (nix/bundled-set.nix). A manifest
    // with no `platformFloor` key -- an older app image, or a build whose
    // catalog names no Platform module at all -- declares NO floor, which
    // refuses nothing. Reading that as "every Platform module is missing" would
    // empty the App Manager of a shell that works.
    static PlatformFloor fromBundledSetManifest(const QByteArray& manifestJson);
    // The same, for a host that has already parsed the manifest -- which every
    // Store shell has, because the core runtime reads its module list out of it.
    static PlatformFloor fromBundledSetManifest(const QJsonObject& manifest);

    // Whether this build said anything. False is a normal state, and is the
    // difference between "nothing is missing" and "we were never told".
    bool isDeclared() const { return m_declared; }

    // Platform modules in this shell's Bundled closure, and Platform modules
    // the catalog holds that are NOT in it.
    const QStringList& present() const { return m_present; }
    const QStringList& absent() const { return m_absent; }

    // The Platform module `name` reaches and this build does not ship, walking
    // `dependencies` (a module name -> the names it declares) transitively.
    // Empty when there is none -- including when the floor was never declared.
    //
    // A name the floor does not carry at all is NOT refused: the floor answers
    // one question, and whether some other module ships a variant this shell
    // can install is package_manager's verdict. Answering it twice is how the
    // two come to disagree.
    QString missingFor(const QString& name,
                       const QHash<QString, QStringList>& dependencies) const;

    // The sentence a refused row carries. One place, because it is read by a
    // user and asserted by the tests on both sides of the seam.
    static QString reasonFor(const QString& missing);

private:
    QStringList m_present;
    QStringList m_absent;
    bool m_declared = false;
};

} // namespace basecamp::appmanager
