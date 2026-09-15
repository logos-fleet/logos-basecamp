#include "ShellPackageSectionDriver.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QQuickItem>
#include <QWidget>

namespace {
const QLatin1String kSectionButton("sidebar.section.package_manager");
const QLatin1String kPaneMessage("packageManagerPane.message");
const QLatin1String kLoading("Loading Package Manager");
} // namespace

ShellPackageSectionDriver::ShellPackageSectionDriver(QWidget* shellWidget, QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
{
}

bool ShellPackageSectionDriver::run()
{
    QQuickItem* button = waitFor(kSectionButton, 5000);
    if (!button) {
        dumpNames(QStringLiteral("the Package Manager section button is not in the sidebar"));
        emit log(QStringLiteral("WRONG: no %1 in the sidebar; the Package Manager section "
                                "cannot be opened at all").arg(kSectionButton));
        return false;
    }
    if (!tap(button)) {
        emit log(QStringLiteral("WRONG: the Package Manager section button could not be "
                                "pressed where it is drawn"));
        return false;
    }

    // THE PAGE IS A WIDGET, not a QML item: MainContainer's content stack is a
    // QStackedWidget and package_manager_ui's slot holds either the plugin's
    // own QQuickWidget or PackageManagerPane. So it is found the widget way,
    // and an ABSENT label is the good case -- it means the plugin's widget
    // replaced the page.
    QElapsedTimer waited;
    waited.start();
    QString text;
    bool loading = false;
    do {
        settle(250);
        auto* label = m_shell->findChild<QLabel*>(kPaneMessage);
        if (!label) {
            emit log(QStringLiteral("PACKAGE MANAGER SECTION OK: package_manager_ui's own "
                                    "view is on the page"));
            return true;
        }
        text = label->text();
        loading = text.contains(kLoading);
    } while (loading && waited.elapsed() < kLoadingVerdictMs);

    if (loading) {
        emit log(QStringLiteral("WRONG: the Package Manager section still says \"%1\" %2 ms "
                                "after it was opened, and nothing is going to arrive")
                     .arg(text).arg(waited.elapsed()));
        return false;
    }

    emit log(QStringLiteral("PACKAGE MANAGER SECTION OK: the page says \"%1\"")
                 .arg(QString(text).replace(QLatin1Char('\n'), QLatin1Char(' '))));
    // HELD, so the page is on screen long enough to be seen. The next driver
    // takes the window back within a frame or two, and a screen recording -- or
    // a screenshot raced against the log -- would otherwise never show the one
    // thing this driver exists to prove.
    settle(1500);
    return true;
}
