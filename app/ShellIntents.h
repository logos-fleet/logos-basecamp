#pragma once

#include <QString>
#include <QStringList>

class IntentRegistry;

namespace ShellIntents {

inline const QStringList kPackageConfirmIntents = {
    QStringLiteral("basecamp.packages.confirm_install"),
    QStringLiteral("basecamp.packages.confirm_uninstall"),
    QStringLiteral("basecamp.packages.confirm_upgrade"),
};

// Of those, the ones no third party has a legitimate reason to raise.
inline const QStringList kRestrictedToPackageManagerUi = {
    QStringLiteral("basecamp.packages.confirm_uninstall"),
    QStringLiteral("basecamp.packages.confirm_upgrade"),
};

// PURE NAVIGATION, and hand-offs to a one. `ok` means "you are there", not "we
// are done", so the broker must not bounce the user back out of a destination
// it was asked to take them to.
inline const QStringList kNavigationIntents = {
    QStringLiteral("basecamp.repositories.manage"),
    QStringLiteral("basecamp.settings.open"),
    QStringLiteral("basecamp.apps.open"),
    QStringLiteral("basecamp.apps.launch"),
    QStringLiteral("basecamp.packages.open"),
};

// Bring a named app forward: `{ "app": "wallet_ui" }`.
inline const QString kAppLaunchIntent = QStringLiteral("basecamp.apps.launch");
inline const QString kPackageManagerAppName =
    QStringLiteral("package_manager_ui");
inline const QString kPackagesOpenIntent =
    QStringLiteral("basecamp.packages.open");
inline const QString kAppLaunchParam  = QStringLiteral("app");

// Which of the shell's own capabilities a URL clicked outside Basecamp may
// reach. A SUBSET of what the shell provides, and deliberately not all of it:
// every entry here is something a web page can cause, so the confirm intents —
// which gate installing and removing packages — are absent and must stay so.
//
// Navigation is the safe set. Nothing crosses a boundary, nothing is
// destructive, and `basecamp.apps.launch` answers identically whether or not
// the named app exists, so a page learns nothing from any of them.
inline const QStringList kWebReachableIntents = kNavigationIntents;

// Declare the shell's provides, hand-offs, uses and requester restrictions.
void registerWith(IntentRegistry* registry,
                  const QString& shellModuleName,
                  const QString& displayName,
                  const QString& iconSource);

} // namespace ShellIntents
