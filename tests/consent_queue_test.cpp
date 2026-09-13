// srcdeps: appmanager/ConsentQueue.cpp
//
// THE SHELL'S HALF OF PER-MODULE CONSENT (slice 29, App Store guideline 4.7.3).
//
// capability_module refuses a call between a Downloaded module and anything else
// until the user has decided, and announces it with `consentRequired`. It cannot
// wait — it is on the dispatch path of every cross-module call in the process —
// so the call fails now, the Shell prompts, and the caller's next attempt is
// decided.
//
// Which leaves the Shell holding what capability_module deliberately did not
// solve: a refused caller RETRIES, a user DISMISSES, and a second module asks
// about the same target. This is where those become one dialog at a time rather
// than a stack of them, and the four rules below are each a way the naive
// version is wrong.
//
// Run: nix build .#unit-tests -L

#include "appmanager/ConsentQueue.h"

#include <QtTest/QtTest>

using basecamp::appmanager::ConsentQueue;

namespace {

// A `consentRequired` payload, as capability_module emits it.
QVariantMap required(const QString& caller, const QString& target,
                     const QString& callerOrigin = QStringLiteral("downloaded"),
                     const QString& targetOrigin = QStringLiteral("bundled"))
{
    return QVariantMap{
        {QStringLiteral("caller"), caller},
        {QStringLiteral("target"), target},
        {QStringLiteral("callerOrigin"), callerOrigin},
        {QStringLiteral("targetOrigin"), targetOrigin},
    };
}

} // namespace

class ConsentQueueTest : public QObject {
    Q_OBJECT

private slots:
    void anAnnouncementBecomesThePendingPrompt()
    {
        ConsentQueue q;
        QVERIFY(!q.hasPending());

        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));

        QVERIFY(q.hasPending());
        QCOMPARE(q.current().caller, QStringLiteral("counter_ui"));
        QCOMPARE(q.current().target, QStringLiteral("chat_module"));
        QCOMPARE(q.current().callerOrigin, QStringLiteral("downloaded"));
    }

    void answeringPopsAndSaysWhatToSend()
    {
        // It returns the decideConsent arguments rather than making the call:
        // the module client belongs to the host, and a queue that made RPCs
        // could not be tested without one.
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        const QVariantMap decision = q.answer(true);

        QCOMPARE(decision.value(QStringLiteral("caller")).toString(),
                 QStringLiteral("counter_ui"));
        QCOMPARE(decision.value(QStringLiteral("target")).toString(),
                 QStringLiteral("chat_module"));
        QVERIFY(decision.value(QStringLiteral("granted")).toBool());
        QVERIFY(!q.hasPending());
        QCOMPARE(q.answered().size(), 1);
    }

    void aDenialIsAnAnswerToo()
    {
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        const QVariantMap decision = q.answer(false);

        QVERIFY(decision.contains(QStringLiteral("granted")));
        QVERIFY(!decision.value(QStringLiteral("granted")).toBool());
        QVERIFY(!q.hasPending());
    }

    // ── one prompt at a time ────────────────────────────────────────────────

    void asecondPairQueuesRatherThanReplacingTheOneOnScreen()
    {
        // Replacing means the user answers a question about a pair they never
        // saw — the consent swap the intent broker has the same rule about.
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));
        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("wallet_module"))));

        QCOMPARE(q.pendingCount(), 2);
        QCOMPARE(q.current().target, QStringLiteral("chat_module"));

        q.answer(true);

        // And only now does the second one come up.
        QCOMPARE(q.current().target, QStringLiteral("wallet_module"));
    }

    void answeringOnePairDoesNotAnswerTheOther()
    {
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("wallet_module")));

        q.answer(true);
        q.answer(false);

        QCOMPARE(q.answered().size(), 2);
        QCOMPARE(q.answered().at(0).value(QStringLiteral("target")).toString(),
                 QStringLiteral("chat_module"));
        QVERIFY(q.answered().at(0).value(QStringLiteral("granted")).toBool());
        QCOMPARE(q.answered().at(1).value(QStringLiteral("target")).toString(),
                 QStringLiteral("wallet_module"));
        QVERIFY(!q.answered().at(1).value(QStringLiteral("granted")).toBool());
    }

    // ── one entry per pair ──────────────────────────────────────────────────

    void aRepeatedAnnouncementForOnePairIsOneQuestion()
    {
        // capability_module dedupes per pair, but a Shell also re-subscribes, a
        // second Downloaded module asks about the same target, and an event can
        // arrive twice. Two dialogs for the same question means the user has to
        // dismiss one after answering it.
        ConsentQueue q;
        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));
        QVERIFY(!q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));
        QVERIFY(!q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));

        QCOMPARE(q.pendingCount(), 1);
    }

    void theReversedPairIsADifferentQuestion()
    {
        // Consent is per ORDERED pair in capability_module, so it is here too:
        // allowing counter_ui to reach chat is not allowing chat to reach
        // counter_ui.
        ConsentQueue q;
        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));
        QVERIFY(q.offer(required(QStringLiteral("chat_module"), QStringLiteral("counter_ui"),
                                 QStringLiteral("bundled"), QStringLiteral("downloaded"))));

        QCOMPARE(q.pendingCount(), 2);
    }

    void namesThatWouldCollideUnderASeparatorDoNot()
    {
        // The key is built with a character a registry name cannot contain. With
        // '-' as the separator, ("a", "b-c") and ("a-b", "c") are one key — and
        // the second question would never be asked.
        ConsentQueue q;
        QVERIFY(q.offer(required(QStringLiteral("a"), QStringLiteral("b-c"))));
        QVERIFY(q.offer(required(QStringLiteral("a-b"), QStringLiteral("c"))));

        QCOMPARE(q.pendingCount(), 2);
    }

    // ── an unanswered pair is not remembered ────────────────────────────────

    void dismissingRecordsNothing()
    {
        // "Not now" is not "never". A Shell that recorded a dismissal as a denial
        // would give capability_module an answer the user did not give — and that
        // answer PERSISTS across restarts, so it would be permanent.
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));

        q.dismiss();

        QVERIFY(!q.hasPending());
        QVERIFY(q.answered().isEmpty());
    }

    void adismissedPairCanBeAskedAgain()
    {
        // Which is the point: a later announcement for the same pair -- the next
        // launch, or after forgetConsent -- has to be able to come back on
        // screen rather than be swallowed here as a duplicate.
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));
        q.dismiss();

        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));
        QVERIFY(q.hasPending());
    }

    void ananswereDpairCanAlsoBeAskedAgain()
    {
        // capability_module will not announce it again — it has a decision — so
        // this only happens after forgetConsent. The queue must not treat its own
        // session history as a reason to swallow the announcement.
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module")));
        q.answer(false);

        QVERIFY(q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"))));
        QVERIFY(q.hasPending());
    }

    // ── malformed and empty ─────────────────────────────────────────────────

    void apayloadThatNamesNoPairIsIgnored()
    {
        ConsentQueue q;
        QVERIFY(!q.offer(QVariantMap{}));
        QVERIFY(!q.offer(required(QString(), QStringLiteral("chat_module"))));
        QVERIFY(!q.offer(required(QStringLiteral("counter_ui"), QString())));
        QVERIFY(!q.hasPending());
    }

    void answeringWithNothingPendingProducesNothingToSend()
    {
        ConsentQueue q;
        QVERIFY(q.answer(true).isEmpty());
        QVERIFY(q.answered().isEmpty());
        q.dismiss();   // and does not crash
    }

    void anEmptyQueueHasNoCurrentPrompt()
    {
        ConsentQueue q;
        QVERIFY(!q.current().isValid());
    }

    // ── what the dialog says ────────────────────────────────────────────────

    void thequestionNamesTheDownloadedPartyAsTheOneYouInstalled()
    {
        ConsentQueue q;
        q.offer(required(QStringLiteral("counter_ui"), QStringLiteral("chat_module"),
                         QStringLiteral("downloaded"), QStringLiteral("bundled")));

        const QString question = q.current().question();
        QVERIFY(question.contains(QStringLiteral("counter_ui")));
        QVERIFY(question.contains(QStringLiteral("chat_module")));
        QVERIFY(question.contains(QStringLiteral("you installed")));
    }

    void thequestionWorksTheOtherWayRoundToo()
    {
        // A Bundled module handing something to a Downloaded one is the case
        // 4.7.3 is actually about, and the sentence has to name the right party.
        ConsentQueue q;
        q.offer(required(QStringLiteral("chat_ui"), QStringLiteral("counter"),
                         QStringLiteral("bundled"), QStringLiteral("downloaded")));

        const QString question = q.current().question();
        QVERIFY(question.contains(QStringLiteral("chat_ui")));
        QVERIFY(question.contains(QStringLiteral("counter")));
        QVERIFY(question.contains(QStringLiteral("you installed")));
        // ...and does not claim the CALLER is the installed one.
        QVERIFY(!question.contains(QStringLiteral("chat_ui”, which you installed")));
    }

    void apayloadWithNoOriginsStillAsksThePlainQuestion()
    {
        ConsentQueue q;
        q.offer(required(QStringLiteral("a"), QStringLiteral("b"), QString(), QString()));

        const QString question = q.current().question();
        QVERIFY(question.contains(QStringLiteral("a")));
        QVERIFY(question.contains(QStringLiteral("b")));
        // No claim about an origin the payload did not carry.
        QVERIFY(!question.contains(QStringLiteral("you installed")));
    }
};

QTEST_MAIN(ConsentQueueTest)
#include "consent_queue_test.moc"
