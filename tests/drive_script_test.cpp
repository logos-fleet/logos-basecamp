// srcdeps: basecamp-shell/src/DriveScript.cpp
//
// WHICH ACCEPTANCE PASSES A LAUNCH ASKED FOR (logos-workspace#155).
//
// The Shell used to drive itself on every launch: one `QTimer::singleShot(0,…)`
// fired the whole set, and the only gating was whether the BUILD contained
// something drivable -- so a build carrying everything drove everything. That
// manufactured state before anyone looked at the app (a hand-driven build had
// an app opened AND closed before the tester touched it), and it made every
// device run pay for every pass.
//
// Driving is now the RUN's choice, and the default is nothing. This is the
// parser that reads the choice off the command line, beside the three flags
// that already worked this way (`--call`, `--repository`, `--consent`).
#include "basecamp-shell/src/DriveScript.h"

#include <QtTest>

using basecamp::shell::DrivePass;
using basecamp::shell::DriveScript;

class DriveScriptTest : public QObject
{
    Q_OBJECT

    static DriveScript parse(const QStringList& tail)
    {
        return DriveScript::fromArguments(QStringList{ "BasecampShell" } + tail);
    }

    // Every pass there is -- so "wants nothing" can be asserted about all of
    // them rather than about the one the test remembered to name.
    static QList<DrivePass> allPasses()
    {
        return { DrivePass::Chat, DrivePass::Apps, DrivePass::Packages,
                 DrivePass::Catalog, DrivePass::Popups, DrivePass::Keyboard,
                 DrivePass::WebApps, DrivePass::WebInput, DrivePass::Modules };
    }

private slots:
    // THE WHOLE POINT OF THE ISSUE. A plain launch is a plain app.
    void anOrdinaryLaunchDrivesNothing()
    {
        const DriveScript s = parse({});
        QVERIFY(s.isEmpty());
        QVERIFY(s.passes().isEmpty());
        QVERIFY(s.refusals().isEmpty());
        for (DrivePass pass : allPasses())
            QVERIFY(!s.wants(pass));
    }

    // ...including a launch that asks for the things that ALREADY worked per
    // run. A catalog install is not a request to drive the Modules tab.
    void theOtherFlagsDoNotTurnDrivingOn()
    {
        const DriveScript s = parse({ "--repository", "http://127.0.0.1:8099/logos-repo.json",
                                      "--install", "web_counter_b",
                                      "--call", "keystore_module.list_accounts",
                                      "--consent", "web_counter_b.package_manager=deny",
                                      "--chat-peer", "0xabc" });
        QVERIFY(s.isEmpty());
        for (DrivePass pass : allPasses())
            QVERIFY(!s.wants(pass));
    }

    void onePassIsTheOnlyPassThatRuns()
    {
        const DriveScript s = parse({ "--drive", "modules" });
        QVERIFY(!s.isEmpty());
        QVERIFY(s.wants(DrivePass::Modules));
        QVERIFY(!s.wants(DrivePass::Apps));
        QVERIFY(!s.wants(DrivePass::WebApps));
        QVERIFY(!s.wants(DrivePass::WebInput));
        QVERIFY(!s.wants(DrivePass::Chat));
        QVERIFY(!s.wants(DrivePass::Packages));
        QVERIFY(!s.wants(DrivePass::Catalog));
        QVERIFY(!s.wants(DrivePass::Keyboard));
        QCOMPARE(s.passes(), QStringList{ "modules" });
    }

    void everyPassHasAName()
    {
        QVERIFY(parse({ "--drive", "chat" }).wants(DrivePass::Chat));
        QVERIFY(parse({ "--drive", "apps" }).wants(DrivePass::Apps));
        QVERIFY(parse({ "--drive", "packages" }).wants(DrivePass::Packages));
        QVERIFY(parse({ "--drive", "catalog" }).wants(DrivePass::Catalog));
        QVERIFY(parse({ "--drive", "popups" }).wants(DrivePass::Popups));
        QVERIFY(parse({ "--drive", "keyboard" }).wants(DrivePass::Keyboard));
        QVERIFY(parse({ "--drive", "web-apps" }).wants(DrivePass::WebApps));
        QVERIFY(parse({ "--drive", "web-input" }).wants(DrivePass::WebInput));
        QVERIFY(parse({ "--drive", "modules" }).wants(DrivePass::Modules));
        // ...and the vocabulary says so, which is what a refusal quotes.
        QCOMPARE(DriveScript::knownPasses(),
                 (QStringList{ "chat", "apps", "packages", "catalog", "popups",
                               "keyboard", "web-apps", "web-input", "modules" }));
    }

    // Composable two ways, because an acceptance run names what it is proving
    // and a device command line is written by hand.
    void passesComposeOnOneFlagOrSeveral()
    {
        const DriveScript commas = parse({ "--drive", "apps,modules" });
        QVERIFY(commas.wants(DrivePass::Apps));
        QVERIFY(commas.wants(DrivePass::Modules));
        QVERIFY(!commas.wants(DrivePass::Chat));

        const DriveScript repeated = parse({ "--drive", "apps", "--drive", "modules" });
        QVERIFY(repeated.wants(DrivePass::Apps));
        QVERIFY(repeated.wants(DrivePass::Modules));
        QVERIFY(!repeated.wants(DrivePass::Chat));

        // Named twice is named once: `passes()` is what the console prints.
        const DriveScript twice = parse({ "--drive", "apps,apps", "--drive", "apps" });
        QCOMPARE(twice.passes(), QStringList{ "apps" });
    }

    // The order a run asks for passes in is not the order they run in -- the
    // sequencing is main.cpp's and it is a set of decisions about what leaves
    // state behind for what. So the console line is canonical, not as-written.
    void passesAreReportedInTheOrderTheyRun()
    {
        const DriveScript s =
            parse({ "--drive", "modules,chat,web-input,keyboard,catalog,popups,web-apps" });
        QCOMPARE(s.passes(),
                 (QStringList{ "chat", "catalog", "popups", "keyboard", "web-apps",
                               "web-input", "modules" }));
    }

    // The historic behaviour, for the runs that really do want all of it -- and
    // spelled out on the command line rather than implied by the build.
    void allIsEveryPass()
    {
        const DriveScript s = parse({ "--drive", "all" });
        for (DrivePass pass : allPasses())
            QVERIFY(s.wants(pass));
        QCOMPARE(s.passes(), DriveScript::knownPasses());
        QVERIFY(s.refusals().isEmpty());
    }

    // EVERY REFUSAL IS KEPT, for the reason the sibling parsers keep theirs: a
    // phone's whole diagnostic surface is one console, and a mistyped flag that
    // silently drives nothing is indistinguishable from a pass that found
    // nothing to do.
    void anUnknownPassIsRefusedByName()
    {
        const DriveScript s = parse({ "--drive", "kitchen-sink" });
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.refusals().at(0).contains("kitchen-sink"));
        QVERIFY(s.refusals().at(0).contains("web-apps"));  // the vocabulary
        QVERIFY(s.passes().isEmpty());
        QVERIFY(!s.isEmpty());                             // it SAID something
    }

    // ...and the good half of a list still runs. A typo in one name is not a
    // reason to throw away the pass beside it.
    void aRefusedNameDoesNotVoidTheOnesBesideIt()
    {
        const DriveScript s = parse({ "--drive", "apps,kitchen-sink,modules" });
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.wants(DrivePass::Apps));
        QVERIFY(s.wants(DrivePass::Modules));
    }

    void anEmptyNameIsRefusedRatherThanIgnored()
    {
        QCOMPARE(parse({ "--drive", "apps," }).refusals().size(), 1);
        QVERIFY(parse({ "--drive", "apps," }).wants(DrivePass::Apps));
    }

    // The trap CatalogSource and ModuleCallScript both guard: a flag whose
    // value was forgotten must not swallow the next flag.
    void aFlagWithNoValueIsRefusedAndEatsNothing()
    {
        const DriveScript last = parse({ "--drive" });
        QCOMPARE(last.refusals().size(), 1);
        QVERIFY(last.passes().isEmpty());

        const DriveScript eaten = parse({ "--drive", "--call", "m.f" });
        QCOMPARE(eaten.refusals().size(), 1);
        QVERIFY(eaten.passes().isEmpty());
        // ...and the flag it did not eat is still there for its own parser.
        QVERIFY(QStringList({ "BasecampShell", "--drive", "--call", "m.f" })
                    .contains("--call"));
    }

    // The two page passes are told apart by name and not by prefix: one opens
    // an app and leaves again, the other types into the form it opens.
    void thePagePassesAreDistinct()
    {
        const DriveScript apps = parse({ "--drive", "web-apps" });
        QVERIFY(apps.wants(DrivePass::WebApps));
        QVERIFY(!apps.wants(DrivePass::WebInput));
        const DriveScript input = parse({ "--drive", "web-input" });
        QVERIFY(input.wants(DrivePass::WebInput));
        QVERIFY(!input.wants(DrivePass::WebApps));
    }

    // A name written the way a person writes it. The vocabulary is closed, so
    // there is nothing a case or a space could otherwise have meant.
    void aNameIsTrimmedAndCaseInsensitive()
    {
        const DriveScript s = parse({ "--drive", " Apps , WEB-APPS " });
        QVERIFY(s.wants(DrivePass::Apps));
        QVERIFY(s.wants(DrivePass::WebApps));
        QVERIFY(s.refusals().isEmpty());
    }
};

QTEST_MAIN(DriveScriptTest)
#include "drive_script_test.moc"
