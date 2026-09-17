#include "ModuleCallScript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace basecamp::appmanager {

namespace {

const QLatin1String kCall("--call");
const QLatin1String kCallTimeout("--call-timeout");

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
    const QLatin1String kStr("str:");
    const QLatin1String kInt("int:");
    const QLatin1String kBool("bool:");
    const QLatin1String kJson("json:");

    const QString text = raw.trimmed();
    // What follows the type prefix -- the value itself.
    const auto value = [&text](QLatin1String prefix) { return text.mid(prefix.size()); };

    if (text.startsWith(kStr)) { *out = value(kStr); return true; }

    if (text.startsWith(kInt)) {
        bool ok = false;
        const qlonglong v = value(kInt).toLongLong(&ok);
        if (!ok) { *refusal = QStringLiteral("'%1' is not an integer").arg(text); return false; }
        *out = QVariant::fromValue(v);
        return true;
    }

    if (text.startsWith(kBool)) {
        const QString v = value(kBool).toLower();
        if (v != QLatin1String("true") && v != QLatin1String("false")) {
            *refusal = QStringLiteral("'%1' is not true or false").arg(text);
            return false;
        }
        *out = (v == QLatin1String("true"));
        return true;
    }

    if (text.startsWith(kJson)) {
        QJsonParseError err{};
        // Wrapped in an array because QJsonDocument::fromJson will not take a
        // bare scalar, and `json:42` is as legitimate as `json:{"a":1}`.
        const QJsonDocument doc =
            QJsonDocument::fromJson(QStringLiteral("[%1]").arg(value(kJson)).toUtf8(), &err);
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

// ONE `<module>.<method>[(<arg>,...)]`, filled into `out`; or false and one
// sentence saying what was wrong with it, which the caller prints under the
// spec it came from.
bool parseCall(const QString& spec, ModuleCall* out, QString* refusal)
{
    // `<module>.<method>` first, then an optional argument list. The dot is
    // sought BEFORE the parenthesis so a dotted argument cannot be mistaken
    // for the method separator.
    const int open = spec.indexOf(QLatin1Char('('));
    const QString head = (open < 0 ? spec : spec.left(open)).trimmed();
    const int dot = head.indexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == head.size() - 1) {
        *refusal = QStringLiteral("expected <module>.<method>");
        return false;
    }

    out->source = spec;
    out->module = head.left(dot);
    out->method = head.mid(dot + 1);

    if (open < 0)
        return true;
    if (!spec.endsWith(QLatin1Char(')'))) {
        *refusal = QStringLiteral("unclosed argument list");
        return false;
    }

    // `f()` is no arguments; `f( )` is the same. `f(,)` is two empty strings
    // and stays two, because an empty string is a value.
    const QString inner = spec.mid(open + 1, spec.size() - open - 2);
    if (inner.trimmed().isEmpty())
        return true;

    bool balanced = false;
    const QStringList pieces = splitArgs(inner, &balanced);
    if (!balanced) {
        *refusal = QStringLiteral("unbalanced quotes or brackets");
        return false;
    }
    for (const QString& piece : pieces) {
        QVariant parsed;
        if (!parseArg(piece, &parsed, refusal))
            return false;
        out->args << parsed;
    }
    return true;
}

} // namespace

ModuleCallScript ModuleCallScript::fromArguments(const QStringList& args)
{
    ModuleCallScript out;
    // The budget in force AT THIS POINT ON THE LINE -- see the header: a
    // `--call-timeout` covers the calls that follow it and a later one narrows
    // it again, so this is carried down the argument list rather than collected
    // from it.
    int timeoutMs = kDefaultTimeoutMs;

    for (int i = 0; i < args.size(); ++i) {
        if (args.at(i) == kCallTimeout) {
            const QString ms = valueAfter(args, i);
            bool ok = false;
            const int parsed = ms.toInt(&ok);
            // Zero is refused with the rest: a call budget of nothing is not a
            // shorter wait, it is a call that cannot succeed.
            if (!ok || parsed <= 0) {
                out.m_refusals << QStringLiteral("--call-timeout needs a positive number of "
                                                 "milliseconds, not '%1'").arg(ms);
                continue;
            }
            timeoutMs = parsed;
            continue;
        }
        if (args.at(i) != kCall)
            continue;

        const QString spec = valueAfter(args, i);
        if (spec.isEmpty()) {
            out.m_refusals << QStringLiteral("--call needs a <module>.<method>(<args>)");
            continue;
        }

        ModuleCall call;
        call.timeoutMs = timeoutMs;
        QString refusal;
        if (parseCall(spec, &call, &refusal))
            out.m_calls << call;
        else
            out.m_refusals << QStringLiteral("%1: %2").arg(spec, refusal);
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
