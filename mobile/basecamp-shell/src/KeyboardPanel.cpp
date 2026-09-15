#include "KeyboardPanel.h"

namespace basecamp::shell {

KeyboardPanel panelDrawn(bool inputMethodVisible, const QRectF& panel, const QRectF& screen)
{
    if (!inputMethodVisible)
        return KeyboardPanel::None;
    // isVisible() turns true on the platform's will-show notification and the
    // geometry arrives WITH it, so a caller that stopped waiting too early
    // holds a panel with no rectangle. Nothing can be said about that one --
    // and saying "shortcut bar" would put a connected hardware keyboard in the
    // log of a run that measured nothing at all.
    if (panel.isEmpty() || screen.isEmpty())
        return KeyboardPanel::Unmeasured;
    return panel.height() < screen.height() / 6 ? KeyboardPanel::ShortcutBar
                                                : KeyboardPanel::Keyboard;
}

} // namespace basecamp::shell
