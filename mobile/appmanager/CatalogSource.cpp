#include "CatalogSource.h"

#include "CatalogEntry.h"

namespace basecamp::appmanager {

namespace {

const QLatin1String kRepository("--repository");
const QLatin1String kTrustSigner("--trust-signer");
const QLatin1String kInstall("--install");

bool isFlag(const QString& arg)
{
    return arg == kRepository || arg == kTrustSigner || arg == kInstall;
}

// The value that follows a flag, or empty when the flag was last on the line or
// the next token is itself a flag. THE SECOND CASE MATTERS: `--repository
// --install x` would otherwise take "--install" as a URL, refuse it as a bad
// scheme, and never parse the install at all -- two wrong answers from one
// missing value.
QString valueAfter(const QStringList& args, int& i)
{
    if (i + 1 >= args.size() || isFlag(args.at(i + 1)))
        return {};
    return args.at(++i);
}

} // namespace

CatalogSource CatalogSource::fromArguments(const QStringList& args)
{
    CatalogSource out;

    for (int i = 0; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (!isFlag(arg))
            continue;

        const QString value = valueAfter(args, i);
        if (value.isEmpty()) {
            out.m_refusals << QStringLiteral("%1 needs a value").arg(arg);
            continue;
        }

        if (arg == kRepository) {
            // ONE repository, and the first one. Last-wins would silently
            // discard a URL that is still on the screen the user typed it on.
            if (!out.m_repositoryUrl.isEmpty()) {
                out.m_refusals << QStringLiteral("already pointed at %1; ignoring %2")
                                      .arg(out.m_repositoryUrl, value);
                continue;
            }
            // The same rule a row's links go through, for the same reason.
            const QString refusal = linkRefusal(value);
            if (!refusal.isEmpty()) {
                out.m_refusals << QStringLiteral("%1: %2").arg(value, refusal);
                continue;
            }
            out.m_repositoryUrl = value;
            continue;
        }

        if (arg == kTrustSigner) {
            // The FIRST separator: a did:jwk is base64url and may carry its own
            // '=' padding, so splitting on every one of them would truncate the
            // DID into something that verifies nothing.
            const int sep = value.indexOf(QLatin1Char('='));
            const QString name = sep < 0 ? QString() : value.left(sep);
            const QString did = sep < 0 ? QString() : value.mid(sep + 1);
            if (name.isEmpty() || !did.startsWith(QLatin1String("did:"))) {
                out.m_refusals << QStringLiteral(
                                      "%1 wants <name>=<did:...>, not '%2'; an anchor that is "
                                      "not a DID trusts nobody while looking like it trusted "
                                      "somebody")
                                      .arg(kTrustSigner, value);
                continue;
            }
            out.m_anchors.append(TrustAnchor{name, did});
            continue;
        }

        if (arg == kInstall) {
            // ONE row, and the first one, for the reason --repository takes the
            // first: last-wins discards a name the user can still see on the
            // line they typed.
            if (!out.m_install.isEmpty()) {
                out.m_refusals << QStringLiteral("already installing %1; ignoring %2")
                                      .arg(out.m_install, value);
                continue;
            }
            out.m_install = value;
        }
    }

    return out;
}

bool CatalogSource::isEmpty() const
{
    return m_repositoryUrl.isEmpty() && m_anchors.isEmpty() && m_install.isEmpty()
        && m_refusals.isEmpty();
}

} // namespace basecamp::appmanager
