// WHICH REFUSALS GET A SCREEN, AND WHAT BECOMES OF IT (logos-workspace#205).
//
// IShellObserver::onUiModuleUnavailable is the host's only negative edge, and
// the Shell used to act on exactly one name through it: package_manager_ui, the
// one module MainContainer hoists into a page of its own rather than docking.
// Every other refusal reached a log. A `ui_qml` app whose declared module this
// device does not have, and a `web` app whose page never opened, were both a
// tile press that did nothing: no window, no message, and no way for the person
// holding the phone to tell a refusal from a slow load.
//
// So a refused app is DOCKED ANYWAY, and what goes in the tab is the refusal.
// The press produces the tab it always produced, the tab carries the host's own
// words, and closing it is the same gesture as closing any other app.
//
// THE RULE IS HERE rather than in MainContainer, which is a QQuickWidget, a
// sidebar, an overlay and a dock area -- none of which a unit test can stand up,
// and none of which is the part that was wrong. `raise` and `drop` are the
// caller's, for the same reason bringUpViewDependencies takes a `load`
// (mobile/basecamp-shell/src/ViewDependencies.h).
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

namespace basecamp::shell {

class AppNotices
{
public:
    // raise(name, reason) puts the screen up, or rewrites the one already
    // there; drop(name) takes it away. Neither is called for an app this class
    // decides has no business having one.
    using RaiseFn = std::function<void(const QString& name, const QString& reason)>;
    using DropFn  = std::function<void(const QString& name)>;

    AppNotices(RaiseFn raise, DropFn drop);

    // The host says `name` is not coming, in its own words. `mounted` is
    // whether the Shell already has that app's REAL widget on screen: a host
    // can refuse a re-load of a module whose window is up, and replacing a
    // working app with an error message would be the worse bug.
    void unavailable(const QString& name, const QString& reason, bool mounted);

    // `name`'s real widget has arrived after all -- the user loaded the module
    // by hand from the Modules tab, which is exactly how #205 was diagnosed.
    // The notice goes FIRST: the workspace keys a dock by module name, so a
    // widget arriving on top of one would be dropped on the floor.
    void arrived(const QString& name);

    // The user closed `name`'s tab. True if what they closed was a notice, in
    // which case there is nothing behind it for the host to unload.
    bool closed(const QString& name);

    bool        holds(const QString& name) const { return m_reasons.contains(name); }
    QStringList names() const { return m_order; }
    QString     reasonFor(const QString& name) const { return m_reasons.value(name); }

private:
    void forget(const QString& name);

    RaiseFn m_raise;
    DropFn  m_drop;
    // The words each notice is showing, and the order they went up in.
    QHash<QString, QString> m_reasons;
    QStringList             m_order;
};

} // namespace basecamp::shell
