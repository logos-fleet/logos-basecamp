#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QTimer;

// PackageManagerPane — the Package Manager section of MainContainer's content
// stack while package_manager_ui is not in it.
//
// The plugin's widget REPLACES this page when it arrives, so on every build
// that ships it this is a page nobody reads for more than a second or two. The
// builds that do not ship it are the reason the page has anything to say: a
// Store shell's Bundled set is data (ADR 0007) and package_manager_ui is a
// desktop `ui_qml` plugin, so on a phone the host refuses the mount the instant
// it is asked for and the section never resolves. It used to sit on "Loading
// Package Manager…" for the rest of the session (logos-workspace#145).
//
// THE DEADLINE IS NOT BELT-AND-BRACES. A load reaches this pane's subject
// through the host, and the host has paths that neither deliver a widget nor
// report a failure — a load parked behind the dependency gate until data that
// may never arrive, a missing-deps popup the user dismissed. Those cannot be
// enumerated from here, and a page that waits on them forever is exactly the
// defect. So an unanswered load is declared dead on a clock.
//
// Qt widgets and nothing else: this compiles inside main_ui, which links Qt
// alone (src/CMakeLists.txt).
class PackageManagerPane : public QWidget
{
    Q_OBJECT

public:
    enum State {
        Idle,          // nothing has asked for the plugin
        Loading,       // a load is in flight and the clock is running
        Unavailable,   // it was refused, or the clock ran out
    };

    explicit PackageManagerPane(QWidget* parent = nullptr);

    State   state() const { return m_state; }
    // The text on screen. The tests read it; nothing else does.
    QString message() const;

    // A load was just asked for. Restarts the clock and clears any previous
    // failure — the user asking again is entitled to the full wait again.
    void beginLoading();

    // The host says it will not come, in its own words. An empty reason is
    // allowed and still produces a screen that reads as an outcome.
    void showUnavailable(const QString& reason);

    // How long an unanswered load may stay unanswered. Settable so a test does
    // not have to spend it.
    void setLoadDeadline(int ms);

    // Generous on purpose: a desktop load spawns a ui-host process and waits on
    // its ready handshake, which PluginLoader alone gives 30 s. Anything
    // tighter would call a slow success a failure.
    static constexpr int kDefaultLoadDeadlineMs = 60000;

private:
    void setMessage(const QString& text);

    QLabel* m_label = nullptr;
    QTimer* m_deadline = nullptr;
    State   m_state = Idle;
    int     m_deadlineMs = kDefaultLoadDeadlineMs;
};
