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

bool parseStep(const QString& word, ConsentScript::Step* out)
{
    if (word == QLatin1String("deny"))            { *out = ConsentScript::Step::Deny;    return true; }
    if (word == QLatin1String("grant"))           { *out = ConsentScript::Step::Grant;   return true; }
    if (word == QLatin1String("dismiss"))         { *out = ConsentScript::Step::Dismiss; return true; }
    if (word == QLatin1String("expect-granted"))  { *out = ConsentScript::Step::ExpectGranted; return true; }
    return false;
}

} // namespace

QString ConsentScript::stepName(Step step)
{
    switch (step) {
    case Step::Deny:          return QStringLiteral("deny");
    case Step::Grant:         return QStringLiteral("grant");
    case Step::Dismiss:       return QStringLiteral("dismiss");
    case Step::ExpectGranted: return QStringLiteral("expect-granted");
    }
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

        const int eq = spec.indexOf(QLatin1Char('='));
        if (eq <= 0 || eq == spec.size() - 1) {
            out.m_refusals << QStringLiteral("%1: expected <caller>.<target>=<steps>").arg(spec);
            continue;
        }

        // The pair. A module name is a registry identifier and carries no dot,
        // so the FIRST one separates the two -- and a head with none of them
        // names one module, which is not a pair and cannot be consented to.
        const QString head = spec.left(eq);
        const int dot = head.indexOf(QLatin1Char('.'));
        if (dot <= 0 || dot == head.size() - 1) {
            out.m_refusals << QStringLiteral("%1: expected <caller>.<target> before '='").arg(spec);
            continue;
        }

        Plan plan;
        plan.source = spec;
        plan.caller = head.left(dot);
        plan.target = head.mid(dot + 1);

        bool refused = false;
        const QStringList words = spec.mid(eq + 1).split(QLatin1Char(','));
        for (const QString& word : words) {
            Step step{};
            if (!parseStep(word.trimmed(), &step)) {
                out.m_refusals
                    << QStringLiteral("%1: '%2' is not a consent step "
                                      "(deny, grant, dismiss, expect-granted)")
                           .arg(spec, word.trimmed());
                refused = true;
                break;
            }
            plan.steps << step;
        }
        if (refused || plan.steps.isEmpty())
            continue;

        out.m_plans << plan;
    }

    return out;
}

} // namespace basecamp::appmanager
