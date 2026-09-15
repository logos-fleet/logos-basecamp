#pragma once

#include "ShellSceneDriver.h"

#include <QString>

// THE PACKAGE MANAGER SECTION, PRESSED, AND WHAT IT THEN SAYS.
//
// The section is the one page of the Shell that waits for a widget:
// package_manager_ui is hoisted into it rather than docked, and until it
// arrives the page is a placeholder. A Store shell's Bundled set is data (ADR
// 0007) and package_manager_ui is a desktop `ui_qml` plugin, so on a phone it
// never arrives at all -- the host refuses the mount on the spot -- and the
// page said "Loading Package Manager…" for the rest of the session
// (logos-workspace#145).
//
// That is a defect no check could see and no console line reported: the mount
// refusal went to the Modules log, which is not the screen the user is looking
// at. So it is asserted here, on the device, in the terms the user reads it in
// -- press the sidebar button a finger would press, then read the page.
//
// It does NOT require the pane to say any particular thing. A build that ships
// package_manager_ui is expected to replace the page entirely; one that does
// not is expected to say why. The failure is the third outcome, and the only
// one this ever saw: a page still claiming to be loading after the wait.
class ShellPackageSectionDriver : public ShellSceneDriver
{
    Q_OBJECT
public:
    ShellPackageSectionDriver(QWidget* shellWidget, QObject* parent = nullptr);

    // Press the section and report. Returns false on the one failure above, or
    // when the sidebar button could not be reached at all.
    bool run();

private:
    // How long the page may stay on "Loading…" before that is the verdict. The
    // refusal path answers inside the press itself; this is the margin for a
    // build where something really is loading.
    static constexpr int kLoadingVerdictMs = 12000;
};
