#pragma once

#include <QString>
#include <QWidget>

class QLabel;

// AppUnavailablePane — what is in the tab of an app that cannot come up
// (logos-workspace#205).
//
// AppNotices decides when one of these belongs on screen; this is what is on
// it. Two things carry the message and both were the defect:
//
//   * the app's name, because the tab the user pressed is all the context they
//     have, and
//   * the host's reason UNEDITED, because "this device does not have
//     delivery_module" and "the image would not load" are different
//     instructions to whoever is reading, and only the host can tell them apart
//     (app/interfaces/IShellHost.h).
//
// Qt widgets and nothing else, like PackageManagerPane beside it: this compiles
// inside main_ui, which links Qt alone (src/CMakeLists.txt).
class AppUnavailablePane : public QWidget
{
    Q_OBJECT

public:
    // `displayLabel` is the app's human name, already resolved by the caller
    // through IShellHost::displayNameFor.
    explicit AppUnavailablePane(const QString& displayLabel,
                                const QString& reason,
                                QWidget* parent = nullptr);

    // A later refusal for the same app rewrites the words in place -- the tab
    // stays where it is.
    void setReason(const QString& reason);

    // The text on screen. The tests read it; nothing else does.
    QString message() const;

private:
    void render();

    QLabel* m_label = nullptr;
    QString m_label_text;
    QString m_reason;
};
