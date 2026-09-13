#include "ModuleCallScript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace basecamp::appmanager {

namespace {

const QLatin1String kCall("--call");

// The value that follows the flag, or empty when the flag was last on the line
// or the next token is itself a flag. The second case is what stops
// `--call --call x` from reading the flag as a call and losing both.
QString valueAfter(const QStringList& args, int& i)
{
    if (i + 1 >= args.size() || args.at(i + 1).startsWith(QLatin1String("--")))
        return {};
    return args.at(++i);
}

// Split on commas AT DEPTH ZERO. A `json:` argument is a whole JSON value and
// commas inside it are its own, so a naive split would cut an object in half
// and report two malformed arguments where there was one good one.
QStringList splitArgs(const QString& text, bool* ok)
{
    QStringList out;
    QString current;
    int depth = 0;
    bool inString = false;
    bool escaped = false;

    for (const QChar c : text) {
        if (inString) {
            current.append(c);
            if (escaped)            escaped = false;
            else if (c == '\\')     escaped = true;
            else if (c == '"')      inString = false;
            continue;
        }
        if (c == '"')                      { inString = true;  current.append(c); continue; }
        if (c == '{' || c == '[')          { ++depth;          current.append(c); continue; }
        if (c == '}' || c == ']')          { --depth;          current.append(c); continue; }
        if (c == ',' && depth == 0)        { out << current;   current.clear();   continue; }
        current.append(c);
    }
    *ok = (depth == 0 && !inString);
    out << current;
    return out;
}

// `[type:]value`. A bare value is a STRING -- see the header for why inference
// is not an option here.
bool parseArg(const QString& raw, QVariant* out, QString* refusal)
{
    const QString text = raw.trimmed();

    auto typed = [&text](const char* prefix) {
        const QLatin1String p(prefix);
        return text.startsWith(p) ? text.mid(p.size()) : QString();
    };

    if (text.startsWith(QLatin1String("str:"))) { *out = text.mid(4); return true; }

    if (text.startsWith(QLatin1String("int:"))) {
        bool ok = false;
        const qlonglong v = typed("int:").toLongLong(&ok);
        if (!ok) { *refusal = QStringLiteral("'%1' is not an integer").arg(text); return false; }
        *out = QVariant::fromValue(v);
        return true;
    }

    if (text.startsWith(QLatin1String("bool:"))) {
        const QString v = typed("bool:").toLower();
        if (v != QLatin1String("true") && v != QLatin1String("false")) {
            *refusal = QStringLiteral("'%1' is not true or false").arg(text);
            return false;
        }
        *out = (v == QLatin1String("true"));
        return true;
    }

    if (text.startsWith(QLatin1String("json:"))) {
        QJsonParseError err{};
        // Wrapped in an array because QJsonDocument::fromJson will not take a
        // bare scalar, and `json:42` is as legitimate as `json:{"a":1}`.
        const QJsonDocument doc =
            QJsonDocument::fromJson(QStringLiteral("[%1]").arg(typed("json:")).toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isArray()) {
            *refusal = QStringLiteral("'%1' is not JSON: %2").arg(text, err.errorString());
            return false;
        }
        *out = doc.array().at(0).toVariant();
        return true;
    }

    *out = text;
    return true;
}

} // namespace

ModuleCallScript ModuleCallScript::fromArguments(const QStringList& args)
{
    ModuleCallScript out;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) != kCall)
            continue;

        const QString spec = valueAfter(args, i);
        if (spec.isEmpty()) {
            out.m_refusals << QStringLiteral("--call needs a <module>.<method>(<args>)");
            continue;
        }

        // `<module>.<method>` first, then an optional argument list. The dot is
        // sought BEFORE the parenthesis so a dotted argument cannot be mistaken
        // for the method separator.
        const int open = spec.indexOf(QLatin1Char('('));
        const QString head = (open < 0 ? spec : spec.left(open)).trimmed();
        const int dot = head.indexOf(QLatin1Char('.'));
        if (dot <= 0 || dot == head.size() - 1) {
            out.m_refusals << QStringLiteral("%1: expected <module>.<method>").arg(spec);
            continue;
        }

        ModuleCall call;
        call.source = spec;
        call.module = head.left(dot);
        call.method = head.mid(dot + 1);

        if (open >= 0) {
            if (!spec.endsWith(QLatin1Char(')'))) {
                out.m_refusals << QStringLiteral("%1: unclosed argument list").arg(spec);
                continue;
            }
            const QString inner = spec.mid(open + 1, spec.size() - open - 2);
            // `f()` is no arguments; `f( )` is the same. `f(,)` is two empty
            // strings and stays two, because an empty string is a value.
            if (!inner.trimmed().isEmpty()) {
                bool balanced = false;
                const QStringList pieces = splitArgs(inner, &balanced);
                if (!balanced) {
                    out.m_refusals << QStringLiteral("%1: unbalanced quotes or brackets")
                                          .arg(spec);
                    continue;
                }
                QString refusal;
                bool bad = false;
                for (const QString& piece : pieces) {
                    QVariant value;
                    if (!parseArg(piece, &value, &refusal)) { bad = true; break; }
                    call.args << value;
                }
                if (bad) {
                    out.m_refusals << QStringLiteral("%1: %2").arg(spec, refusal);
                    continue;
                }
            }
        }

        out.m_calls << call;
    }

    return out;
}

QStringList ModuleCallScript::modules() const
{
    QStringList out;
    for (const ModuleCall& call : m_calls)
        if (!out.contains(call.module))
            out << call.module;
    return out;
}

} // namespace basecamp::appmanager
