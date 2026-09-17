#include "DriveScript.h"

namespace basecamp::shell {

namespace {

const QLatin1String kDrive("--drive");
const QLatin1String kAll("all");

// The vocabulary, ONCE, in run order. knownPasses() and passes() both read it,
// so a pass cannot be spellable and unprintable or the other way round.
// `takesOption` is whether `<name>:<something>` means anything to the pass; an
// option on a pass that has no use for one is refused rather than dropped.
struct Named {
    DrivePass     pass;
    QLatin1String name;
    bool          takesOption;
};

const Named kPasses[] = {
    { DrivePass::Chat,      QLatin1String("chat"),       false },
    { DrivePass::Apps,      QLatin1String("apps"),       false },
    { DrivePass::Packages,  QLatin1String("packages"),   false },
    // ...and whether it PRESSES the page it reads (logos-workspace#249):
    // `install`, or `install=<package>` for a catalog with several installable
    // rows. What the option MEANS is ShellCatalogPageDriver's, like every other
    // option here.
    { DrivePass::Catalog,   QLatin1String("catalog"),    true  },
    { DrivePass::Popups,    QLatin1String("popups"),     false },
    { DrivePass::Keyboard,  QLatin1String("keyboard"),   false },
    { DrivePass::WebApps,   QLatin1String("web-apps"),   false },
    // ...which flow of the app's it walks (logos-workspace#238). The names are
    // WebDriveFlows', not this parser's.
    { DrivePass::WebInput,  QLatin1String("web-input"),  true  },
    { DrivePass::WebBudget, QLatin1String("web-budget"), false },
    { DrivePass::Modules,   QLatin1String("modules"),    false },
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
    // was asked for. An OPTION is kept per pass, also once each and also in the
    // order it was asked: `--drive web-input:a,web-input:b` is one pass walking
    // two flows, and the order is the run's to choose.
    const auto select = [&out](DrivePass pass, const QString& option) {
        if (!out.m_passes.contains(pass))
            out.m_passes << pass;
        if (!option.isEmpty() && !out.m_options[pass].contains(option))
            out.m_options[pass] << option;
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
            // `<pass>:<option>`, split on the FIRST colon so an option may
            // carry one. A pass name never does.
            const QString whole = piece.trimmed();
            const int colon = whole.indexOf(QLatin1Char(':'));
            const QString name = (colon < 0 ? whole : whole.left(colon)).trimmed().toLower();
            const QString option = colon < 0 ? QString() : whole.mid(colon + 1).trimmed();

            if (name == kAll) {
                // `all` is every pass at its default. An option on it would have
                // to mean the same thing to ten passes, and means nothing to
                // nine of them.
                if (!option.isEmpty()) {
                    out.m_refusals << QStringLiteral(
                        "--drive 'all' takes no option (got ':%1'): name the pass instead")
                                          .arg(option);
                    continue;
                }
                for (const Named& named : kPasses)
                    select(named.pass, QString());
            } else if (const Named* named = passNamed(name)) {
                if (!option.isEmpty() && !named->takesOption) {
                    out.m_refusals << QStringLiteral("--drive '%1' takes no option (got ':%2')")
                                          .arg(name, option);
                    continue;
                }
                select(named->pass, option);
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

QStringList DriveScript::optionsFor(DrivePass pass) const
{
    return m_options.value(pass);
}

QStringList DriveScript::passes() const
{
    QStringList out;
    for (const Named& named : kPasses) {
        if (!m_passes.contains(named.pass))
            continue;
        const QStringList options = m_options.value(named.pass);
        if (options.isEmpty()) {
            out << named.name;
            continue;
        }
        for (const QString& option : options)
            out << QStringLiteral("%1:%2").arg(named.name, option);
    }
    return out;
}

} // namespace basecamp::shell
