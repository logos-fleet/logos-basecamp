// WHAT A `web` APP CAN BE DRIVEN THROUGH, as data -- and the one kind of
// verdict a page's controls cannot give.
//
// WHY THIS IS ITS OWN FILE (logos-workspace#238). ShellWebInputDriver arrived
// with one flow, written into the driver as a `flowFor(app)` that answered a
// SINGLE flow per app: wallet_ui's Advanced-tab seed import. The wallet then
// grew a second thing worth driving -- the Private tab's accumulator sync, the
// `Sync now` and `Cancel` that #235 shipped and could not check on a device --
// and with one flow per app the only way to reach it was to REPLACE the first,
// trading one uncheckable flow for another. So an app carries several NAMED
// flows and a run says which it wants (`--drive web-input:<flow>`), and the
// catalogue lives here rather than inside the driver: it is data about modules,
// it is what the next `web` app will add to, and it can be stated in a unit
// test on a desktop with no webview in it.
//
// A STEP IS ONE OF FOUR THINGS.
//
//   Press   a control, by whichever handle it has (WebPageInput.h).
//   Type    into a field and leave it holding that.
//   Read    a field back and require it to hold something -- the module's own
//           state, arrived at through real key events.
//   Await   ...and the one this issue exists for: A VERDICT THAT IS A PAGE
//           CONSOLE LINE rather than a field's contents. The private sync's
//           whole state lives in a PROP the view renders, not in anything a
//           driver can read off a control: the percentage, the block a cancel
//           kept, the `state` itself. All of it is on the page's console, which
//           the container already forwards to the app's log, so the honest
//           verdict is the line the MODULE published and not a pixel.
//
// AND AN `Await` CAN REQUIRE THAT A NUMBER MOVED. "a `private sync running:`
// line appeared" is satisfied by the line `startPrivateSync` publishes before
// it has asked for a single window -- it proves the button was pressed and
// nothing at all about the walk. `Want::Moved` takes the first reading as a
// baseline and waits for one that differs from it, which is the difference
// between "the wallet said it started" and "the accumulator sync is running".
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace basecamp::shell {

// ONE LINE THE PAGE MUST PRINT, described as data: which lines are this
// watch's, which JSON field on one is the reading, and what the reading has to
// do.
struct WebPageWatch {
    // The line must contain this. A module's announcements are prefixed with
    // its own name by the time they reach the log, so this is a substring and
    // never the whole line.
    QString marker;
    // A JSON field of the object on that line. Every announcement this drives
    // is `<something>: {…}`, so the object is whatever follows the first `{`.
    QString field;
    enum class Want {
        Present,  // the field is there at all, with a value that is not null
        Moved,    // ...and a later reading differs from the first one seen
    };
    Want want = Want::Present;
    // READ THE WHOLE FLOW'S LINES, not just the ones since the step before.
    //
    // For a `Moved` watch this is usually what is meant and is sometimes the
    // ONLY thing that can be meant: the wallet's private sync is over in four
    // seconds on a simulator against public Sepolia, so by the time the cancel
    // this flow presses has landed, every line that carried the movement is
    // already behind the cursor. A watch that asks "did this number move while
    // the walk ran" has to be allowed to look at the walk, not at what came
    // after it.
    bool overTheWholeFlow = false;
    // How long this step gets. An `Await` waits on a MODULE doing work rather
    // than on a page answering a script, so the budgets here are the module's
    // and are written at each flow.
    int budgetMs = 0;
    // What the step is proving, in the words the pass prints. Also what its
    // failure line quotes, because "the page said nothing about 'percent'"
    // tells a reader nothing on its own.
    QString what;
};

// THE WATCH, FED LINES. Separate from the driver that collects them so the
// whole reading -- which line counts, what the field says, whether it moved --
// can be stated against fixed strings with no page anywhere.
class WebPageWatcher
{
public:
    explicit WebPageWatcher(WebPageWatch watch);

    // Offer one page line. True once the watch is settled; every line after
    // that is ignored, so a caller may keep offering without checking.
    bool offer(const QString& line);

    bool settled() const { return m_settled; }
    // The reading that settled it.
    QString reading() const { return m_reading; }
    // The first reading seen, which `Moved` is measured against. Empty when no
    // line ever carried the field.
    QString baseline() const { return m_baseline; }
    // How many of this watch's lines were seen, field or no field. It is what
    // separates "the module never said this" from "it said it and the number
    // never moved" -- two different findings that time out identically.
    int linesSeen() const { return m_linesSeen; }

    // The field's value on `line`, when the line is one of `marker`'s and the
    // field is there and is not null. A number comes back as its text.
    static std::optional<QString> readingOf(const QString& line, const QString& marker,
                                            const QString& field);

private:
    WebPageWatch m_watch;
    bool         m_settled = false;
    int          m_linesSeen = 0;
    QString      m_baseline;
    bool         m_haveBaseline = false;
    QString      m_reading;
};

// ONE STEP OF A FLOW. `control` is named the way the page names it -- an
// accessible name, or the objectName a Logos view already carries for the
// desktop inspector; WebPageInput.h has the account of the two handles.
struct WebDriveStep {
    enum class Act {
        Press,      // press `control`
        PressAhead, // ...arm the press in the page and do not wait for it
        Type,       // type `text` into `control`
        Read,       // `control` must hold `text`
        Await,      // `watch` must be satisfied by a page console line
    };
    Act          act = Act::Press;
    QString      control;
    QString      text;
    WebPageWatch watch;
    // PressAhead only: how long the PAGE waits before pressing.
    int          afterMs = 0;

    static WebDriveStep press(const QString& control);

    // A PRESS SENT BEFORE THE HOST STOPS ANSWERING.
    //
    // A `web` module's outbound call crosses to the host and the host answers
    // it SYNCHRONOUSLY, so a module call that takes seconds stops the host's
    // event loop -- and this driver runs on it. A flow whose next press has to
    // land DURING such a call therefore cannot wait for the previous press to
    // be confirmed: the confirmation is sitting in a queue the host will not
    // read until the work is done. So both presses go into the page first and
    // the PAGE sequences them on its own timer, which is what a person with two
    // fingers does anyway.
    //
    // Nothing is asserted about the press itself. What it did is asserted by
    // the `Await` steps after it, off the module's own announcements -- which
    // is a stronger claim than "the page says it dispatched a pointer event".
    static WebDriveStep pressAhead(const QString& control, int afterMs);

    static WebDriveStep type(const QString& control, const QString& text);
    static WebDriveStep read(const QString& control, const QString& holds);
    static WebDriveStep await(const WebPageWatch& watch);
};

// A NAMED FLOW through one app: what `--drive web-input:<name>` selects.
struct WebDriveFlow {
    QString             name;
    QString             app;
    // The sentence the pass prints when every step passed -- the claim the run
    // is evidence for, in the shape the other passes print theirs.
    QString             verdict;
    QList<WebDriveStep> steps;

    bool isEmpty() const { return steps.isEmpty(); }
};

// THE CATALOGUE. Everything the Shell knows how to walk inside a page.
class WebDriveFlows
{
public:
    // Every flow for an app, in the order they are offered. The FIRST is what a
    // run that named no flow gets, so it is also the compatibility contract:
    // `--drive web-input` still drives what it drove before this file existed.
    static QList<WebDriveFlow> forApp(const QString& app);

    // Their names, for a refusal that lists what the app does have.
    static QStringList namesFor(const QString& app);

    // The named flow, or the app's first when `name` is empty. An empty flow
    // when the app has no such flow -- or no flows at all.
    static WebDriveFlow select(const QString& app, const QString& name);
};

} // namespace basecamp::shell
