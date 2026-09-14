// srcdeps: appmanager/ConsentScript.cpp
//
// WHAT A LAUNCH WAS ASKED TO ANSWER THE 4.7.3 PROMPT WITH (logos-workspace#104).
//
// The consent criterion is four things in a row and only the first is visible on
// a screen: the prompt appears, a denial fails the call, a grant lets it
// through, and the grant survives a relaunch. The last three are ANSWERS, and on
// a phone the only way to give one to a run is the app's own command line -- the
// same door `--install` and `--call` come through.
//
// So this covers the parsing and nothing else: which strings become a plan,
// which become a refusal, and that a refusal is KEPT. A phone's whole diagnostic
// surface is one console, and a mistyped flag that silently does nothing is
// indistinguishable from a gate that never fired -- which is precisely the
// failure this whole flag exists to rule out.
//
// Run: nix build .#unit-tests -L

#include "appmanager/ConsentScript.h"

#include <QtTest/QtTest>

using basecamp::appmanager::ConsentScript;

class ConsentScriptTest : public QObject
{
    Q_OBJECT

private slots:
    void PlainLaunchIsEmpty()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell")});
        QVERIFY(s.isEmpty());
        QVERIFY(s.plans().isEmpty());
        QVERIFY(s.refusals().isEmpty());
    }

    void UnknownArgumentsAreLeftAlone()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--platform"),
             QStringLiteral("offscreen")});
        QVERIFY(s.isEmpty());
    }

    // The first launch of the two: deny, watch the caller's next attempt fail,
    // then grant and watch it succeed.
    void APairAndItsAnswersInOrder()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("web_counter_b.package_manager=deny,grant")});
        QVERIFY(s.refusals().isEmpty());
        QCOMPARE(s.plans().size(), 1);

        const ConsentScript::Plan p = s.plans().first();
        QCOMPARE(p.caller, QStringLiteral("web_counter_b"));
        QCOMPARE(p.target, QStringLiteral("package_manager"));
        QCOMPARE(p.steps.size(), 2);
        QCOMPARE(p.steps.at(0), ConsentScript::Step::Deny);
        QCOMPARE(p.steps.at(1), ConsentScript::Step::Grant);
        QCOMPARE(p.source, QStringLiteral("web_counter_b.package_manager=deny,grant"));
    }

    // The second launch: the grant is on disk before anything asks.
    void ASecondLaunchAssertsThePersistedGrant()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("web_counter_b.package_manager=expect-granted")});
        QCOMPARE(s.plans().size(), 1);
        QCOMPARE(s.plans().first().steps, QList<ConsentScript::Step>{
            ConsentScript::Step::ExpectGranted});
    }

    void EveryStepWordIsAccepted()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("a.b=deny,grant,dismiss,expect-granted")});
        QVERIFY(s.refusals().isEmpty());
        QCOMPARE(s.plans().first().steps.size(), 4);
        for (ConsentScript::Step step : s.plans().first().steps)
            QVERIFY(!ConsentScript::stepName(step).isEmpty());
    }

    // Repeatable: two Downloaded modules asking about two targets is the shape
    // the queue exists for, and a run has to be able to answer both.
    void TwoPairsAreTwoPlansInOrder()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"),
             QStringLiteral("--consent"), QStringLiteral("a.chat_module=deny"),
             QStringLiteral("--consent"), QStringLiteral("b.package_manager=grant")});
        QCOMPARE(s.plans().size(), 2);
        QCOMPARE(s.plans().at(0).caller, QStringLiteral("a"));
        QCOMPARE(s.plans().at(1).target, QStringLiteral("package_manager"));
    }

    // ── the refusals, which are the reason this is parsed at all ──────────

    void AFlagWithNoValueIsRefused()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent")});
        QVERIFY(s.plans().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(!s.isEmpty());   // it asked for something and it did not happen
    }

    // `--consent --install x` must not read the next flag as its value and lose
    // both of them.
    void AFlagFollowedByAnotherFlagIsRefused()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("--install"), QStringLiteral("web_counter_b")});
        QVERIFY(s.plans().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
    }

    void AHeadThatIsNotAPairIsRefused()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("web_counter_b=deny")});
        QVERIFY(s.plans().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.refusals().first().contains(QStringLiteral("<caller>.<target>")));
    }

    void MissingStepsAreRefused()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("a.b=")});
        QVERIFY(s.plans().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
    }

    // An unknown word takes the WHOLE plan with it. Half a script is worse than
    // none: "deny,grnat" would otherwise deny and then quietly never grant, and
    // the run would report a refusal that a grant was supposed to clear.
    void AnUnknownStepRefusesTheWholePlan()
    {
        const ConsentScript s = ConsentScript::fromArguments(
            {QStringLiteral("BasecampShell"), QStringLiteral("--consent"),
             QStringLiteral("a.b=deny,grnat")});
        QVERIFY(s.plans().isEmpty());
        QCOMPARE(s.refusals().size(), 1);
        QVERIFY(s.refusals().first().contains(QStringLiteral("grnat")));
    }
};

QTEST_MAIN(ConsentScriptTest)
#include "consent_script_test.moc"
