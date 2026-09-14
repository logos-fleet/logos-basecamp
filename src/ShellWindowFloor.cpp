#include "ShellWindowFloor.h"

#include <QGuiApplication>
#include <QScreen>

namespace basecamp {

QSize shellWindowFloor(const QSize& desired, const QSize& screen)
{
    // isEmpty() is `w < 1 || h < 1`, so it covers the invalid sizes too --
    // a default-constructed QSize is (-1, -1), and both mean "no screen".
    if (screen.isEmpty())
        return desired;
    return desired.boundedTo(screen);
}

QSize shellWindowFloorForThisScreen(const QSize& desired)
{
    // availableSize rather than size: on a desktop it is the screen minus the
    // panels a window may not cover, and on both phones the platform reports
    // the whole display here and applies the safe-area insets to the window
    // itself.
    const QScreen* screen = QGuiApplication::primaryScreen();
    return shellWindowFloor(desired, screen ? screen->availableSize() : QSize());
}

} // namespace basecamp
