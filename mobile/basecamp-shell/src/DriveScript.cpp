#include "DriveScript.h"

namespace basecamp::shell {

namespace {

const QLatin1String kDrive("--drive");
const QLatin1String kAll("all");

// The vocabulary, ONCE, in run order. knownPasses() and passes() both read it,
// so a pass cannot be spellable and unprintable or the other way round.
struct Named {
    DrivePass   pass;
    QLatin1String name;
};

const Named kPasses[] = {
    { DrivePass::Chat,     QLatin1String("chat") },
    { DrivePass::Apps,     QLatin1String("apps") },
    { DrivePass::Packages, QLatin1String("packages") },
    { DrivePass::Keyboard, QLatin1String("keyboard") },
    { DrivePass::WebApps,  QLatin1String("web-apps") },
    { DrivePass::Modules,  QLatin1String("modules") },
};

// The pass a name spells, or null. `all` is deliberately absent: it stands for
// every pass at once rather than for one, and only fromArguments() has a use
// for it.
const Named* passNamed(const QString& name)
{
    for (const Named& named : kPasses)
        if (name == named.name)
            return &named;
    return nullptr;
}

// The value that follows the flag, or empty when the flag was last on the line
// or the next token is itself a flag. The second case is what stops
// `--drive --call m.f` from reading the flag as a pass name and losing both.
QString valueAfter(const QStringList& args, int& i)
{
    if (i + 1 >= args.size() || args.at(i + 1).startsWith(QLatin1String("--")))
        return {};
    return args.at(++i);
}

} // namespace

QStringList DriveScript::knownPasses()
{
    QStringList out;
    for (const Named& named : kPasses)
        out << named.name;
    return out;
}

DriveScript DriveScript::fromArguments(const QStringList& args)
{
    DriveScript out;

    // Quoted by every refusal, so a name that does not exist arrives with the
    // whole vocabulary beside it rather than sending the reader to the README.
    const QString vocabulary = knownPasses().join(QLatin1String(", "));

    // Recorded once, in run order, however many times or in whatever order it
    // was asked for.
    const auto select = [&out](DrivePass pass) {
        if (!out.m_passes.contains(pass))
            out.m_passes << pass;
    };

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) != kDrive)
            continue;

        const QString spec = valueAfter(args, i);
        if (spec.isEmpty()) {
            out.m_refusals << QStringLiteral("--drive needs a pass: %1").arg(vocabulary);
            continue;
        }

        // A list, and a bad name in it refuses only itself -- a typo is not a
        // reason to throw away the pass written beside it.
        for (const QString& piece : spec.split(QLatin1Char(','))) {
            const QString name = piece.trimmed().toLower();
            if (name == kAll) {
                for (const Named& named : kPasses)
                    select(named.pass);
            } else if (const Named* named = passNamed(name)) {
                select(named->pass);
            } else {
                out.m_refusals
                    << QStringLiteral("--drive '%1' is not a pass: %2").arg(name, vocabulary);
            }
        }
    }

    return out;
}

bool DriveScript::wants(DrivePass pass) const
{
    return m_passes.contains(pass);
}

QStringList DriveScript::passes() const
{
    QStringList out;
    for (const Named& named : kPasses)
        if (m_passes.contains(named.pass))
            out << named.name;
    return out;
}

} // namespace basecamp::shell
