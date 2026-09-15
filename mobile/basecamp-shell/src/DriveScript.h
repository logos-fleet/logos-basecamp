#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace basecamp::shell {

// ONE ACCEPTANCE PASS the Shell knows how to drive, in the order the passes run
// when a launch asks for several. The order is not arbitrary and it is not this
// enum's to choose -- it is main.cpp's sequencing, recorded here so that
// `passes()` prints a run in the order it will happen:
//
//   Chat      first, because "cold start to Chat usable" is how long a user
//             waits before they can type, and any other pass' settle in front
//             of it would be reported as part of the chat bring-up
//   Apps      then the app, opened from the sidebar -- before the Modules tab,
//             which unloads and reloads a core module underneath it
//   Packages  the Shell's own chrome, while the Shell still has the window
//   Keyboard  the app's input fields -- after the section pass, because it is
//             the one that leaves a platform panel over the Shell and the one
//             that puts itself back where it started
//   WebApps   the step that hands the window to a platform page
//   WebInput  ...and typing into the form on that page, which needs one open
//   Modules   last: it is the one that changes what is loaded
enum class DrivePass {
    Chat,
    Apps,
    Packages,
    Keyboard,
    WebApps,
    WebInput,
    Modules,
};

// WHICH PASSES THIS RUN ASKED FOR. Nothing, unless it said so.
//
// WHY IT HAS TO EXIST (logos-workspace#155). The Shell used to drive itself on
// every launch: one `QTimer::singleShot(0, …)` fired the catalog install, the
// `--call` script, the consent answers, the chat bring-up, the app, the web app
// and the Modules tab, and the only gating was `hasWork()` -- which asks whether
// the BUILD contains something drivable. So a build carrying everything drove
// everything, and there was no way to decline.
//
// Seven feature commits in four days put it there, each adding its own on-device
// proof to the same lambda, and every one of them was individually reasonable: a
// phone has no inspector, `simctl`/`devicectl` have no tap verb, and as of Xcode
// 27 `idb ui tap` is gone too, so an app that proves things about itself was the
// only way to get evidence off a device. The sum is what nobody chose:
//
//   - it MANUFACTURES STATE BEFORE ANYONE LOOKS. A hand-driven build had an app
//     loaded and a web app opened *and closed* before the tester reached it, and
//     several defects reported from manual testing were driver aftermath rather
//     than the app's resting state
//   - it CREATES FAILURES OF ITS OWN TIMING -- a crash that happens because a
//     driver closes a web app is not something a user would have done in that
//     order
//   - EVERY DEVICE RUN PAYS FOR ALL OF IT, including an agent that wanted one
//     measurement
//
// So driving is a property of the RUN, configured for the task at hand:
//
//     --drive modules              one pass
//     --drive chat,apps            composable, and repeatable across flags
//     --drive all                  the historic behaviour, spelled out
//     (nothing)                    a plain app, on a phone as on the desktop
//
// THE PASSES THEMSELVES ARE UNCHANGED and print exactly what they printed
// before: roughly ten issues quote their assertions as on-device evidence
// (`SHELL MODULES TAB LISTS WHAT THE APP HAS`, `APP SHOWN: … ms after the
// sidebar tile was pressed`). Only their AUTOMATIC invocation is gone.
//
// NOT EVERYTHING THE COMMAND LINE CAN ASK FOR IS A PASS. `--repository` /
// `--install`, `--call` and `--consent` already worked this way -- each is its
// own script, with its own work to do only when a flag named some -- and they
// are untouched. This covers the four that had no flag at all.
//
// EVERY REFUSAL IS KEPT rather than dropped, for the reason CatalogSource and
// ModuleCallScript keep theirs: a phone's whole diagnostic surface is one
// console, and a mistyped pass name that silently drives nothing is
// indistinguishable from a pass that found nothing to do.
class DriveScript {
public:
    // Parse an argument list -- QCoreApplication::arguments(), program name
    // included. Anything unrecognised belongs to Qt or to the platform and is
    // left alone.
    static DriveScript fromArguments(const QStringList& args);

    // Every pass there is, in the order they run. What a refusal quotes, and
    // what `--drive all` means.
    static QStringList knownPasses();

    bool wants(DrivePass pass) const;

    // The passes this run asked for, once each, IN THE ORDER THEY WILL RUN
    // rather than the order they were written -- the sequencing is the app's,
    // so the console line should read as the run it is about to be.
    QStringList passes() const;

    // One sentence per refused argument, in the order they were met.
    QStringList refusals() const { return m_refusals; }

    // Whether this launch said anything at all about driving. A launch that
    // asked for a pass by a name that does not exist is NOT empty: it said
    // something, and what it said is in refusals().
    bool isEmpty() const { return m_passes.isEmpty() && m_refusals.isEmpty(); }

private:
    QList<DrivePass> m_passes;
    QStringList      m_refusals;
};

} // namespace basecamp::shell
