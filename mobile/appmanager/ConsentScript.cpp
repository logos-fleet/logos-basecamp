#include "ConsentScript.h"

namespace basecamp::appmanager {

namespace {

const QLatin1String kConsent("--consent");

// The value that follows the flag, or empty when the flag was last on the line
// or the next token is itself a flag -- the same rule CatalogSource and
// ModuleCallScript use, and for the same reason: `--consent --install x` must
// not swallow the next flag.
QString valueAfter(const QStringList& args, int& i)
{
    if (i + 1 >= args.size() || args.at(i + 1).startsWith(QLatin1String("--")))
        return {};
    return args.at(++i);
}

// THE STEP WORDS, SPELLED ONCE. The parser, `stepName` and the sentence a bad
// word is refused with all read these rows, so a fifth step cannot be taught to
// one of the three and forgotten in the other two.
struct StepWord {
    const char*         word;
    ConsentScript::Step step;
};

const StepWord kStepWords[] = {
    {"deny",           ConsentScript::Step::Deny},
    {"grant",          ConsentScript::Step::Grant},
    {"dismiss",        ConsentScript::Step::Dismiss},
    {"expect-granted", ConsentScript::Step::ExpectGranted},
};

bool parseStep(const QString& word, ConsentScript::Step* out)
{
    for (const StepWord& known : kStepWords) {
        if (word == QLatin1String(known.word)) {
            *out = known.step;
            return true;
        }
    }
    return false;
}

// Every step word, for the refusal that lists what was expected instead.
QString knownStepWords()
{
    QStringList words;
    for (const StepWord& known : kStepWords)
        words << QLatin1String(known.word);
    return words.join(QStringLiteral(", "));
}

// ONE `<caller>.<target>=<steps>`, or the sentence saying why it is not one.
// The refusal is the other half of the answer, never a silent drop: a phone's
// whole diagnostic surface is one console.
bool parsePlan(const QString& spec, ConsentScript::Plan* plan, QString* refusal)
{
    const int eq = spec.indexOf(QLatin1Char('='));
    if (eq <= 0 || eq == spec.size() - 1) {
        *refusal = QStringLiteral("%1: expected <caller>.<target>=<steps>").arg(spec);
        return false;
    }

    // The pair. A module name is a registry identifier and carries no dot, so
    // the FIRST one separates the two -- and a head with none of them names one
    // module, which is not a pair and cannot be consented to.
    const QString head = spec.left(eq);
    const int dot = head.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == head.size() - 1) {
        *refusal = QStringLiteral("%1: expected <caller>.<target> before '='").arg(spec);
        return false;
    }

    plan->source = spec;
    plan->caller = head.left(dot);
    plan->target = head.mid(dot + 1);

    // AN UNKNOWN WORD TAKES THE WHOLE PLAN WITH IT. Half a script is worse than
    // none: "deny,grnat" would otherwise deny and then quietly never grant, and
    // the run would report a refusal a grant was supposed to clear.
    const QStringList words = spec.mid(eq + 1).split(QLatin1Char(','));
    for (const QString& word : words) {
        const QString wanted = word.trimmed();
        ConsentScript::Step step{};
        if (!parseStep(wanted, &step)) {
            *refusal = QStringLiteral("%1: '%2' is not a consent step (%3)")
                           .arg(spec, wanted, knownStepWords());
            return false;
        }
        plan->steps << step;
    }
    return true;
}

} // namespace

QString ConsentScript::stepName(Step step)
{
    for (const StepWord& known : kStepWords)
        if (known.step == step)
            return QString::fromLatin1(known.word);
    return QStringLiteral("unknown");
}

ConsentScript ConsentScript::fromArguments(const QStringList& args)
{
    ConsentScript out;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) != kConsent)
            continue;

        const QString spec = valueAfter(args, i);
        if (spec.isEmpty()) {
            out.m_refusals << QStringLiteral("--consent needs <caller>.<target>=<steps>");
            continue;
        }

        Plan plan;
        QString refusal;
        if (parsePlan(spec, &plan, &refusal))
            out.m_plans << plan;
        else
            out.m_refusals << refusal;
    }

    return out;
}

} // namespace basecamp::appmanager
