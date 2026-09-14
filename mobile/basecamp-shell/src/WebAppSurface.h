// THE PLACEHOLDER A WEB-CONTAINER APP IS DOCKED AS.
//
// A `ui_qml` member of the Bundled set has a widget, and the Shell docks it: a
// tab in the workspace, the sidebar down the left, the navigation bar above,
// and a close button on the tab. A `web` module has NO widget -- its UI is a
// platform page the container mounted at the WINDOW's size behind the Shell's
// own surface, and "open it" is a z-order instruction. Brought forward it
// covers the Shell entirely and there is no way back out of the app (#110).
//
// This closes that gap without inventing a second navigation model: the host
// docks one of these for a web app exactly as it docks a `ui_qml` widget, so
// the Shell draws the same chrome around the same hole -- and then this says
// WHERE the hole ended up, in the window's coordinates, every time it moves.
// The host hands that rect to the Web container, the page is inset to it, and
// the chrome the user needs is outside the page rather than under it.
//
// IT PAINTS NOTHING, and nothing it could paint would be seen: the page is a
// native view in front of Qt's entire surface, so whatever is behind it is
// invisible by construction.
//
// WINDOW COORDINATES, QT LOGICAL PIXELS. The platform half converts -- UIKit
// points on iOS are Qt's logical pixels, Android's view coordinates are not --
// and doing it here would make this class know which phone it is on.
#pragma once

#include <QRect>
#include <QString>
#include <QWidget>

#include <QPointer>
#include <QVector>

class WebAppSurface : public QWidget
{
    Q_OBJECT

public:
    explicit WebAppSurface(QString moduleName, QWidget* parent = nullptr);

    const QString& moduleName() const { return m_moduleName; }

    // Where this widget sits in its top-level window, or an EMPTY rect when it
    // is not on screen. Empty is not a degenerate rect here: it is what the
    // container reads as "the whole window", so it must never be published for
    // a surface the Shell has laid out -- hence onScreen() beside it rather
    // than a caller testing isEmpty().
    QRect pageRect() const;
    bool onScreen() const;

signals:
    // Emitted only when the answer CHANGED. Each one costs the host a
    // show()/hideAll() on the container, which spends the live-runtime budget
    // and prints the app's memory; repeating an unchanged placement on every
    // polish pass would turn a layout into a log.
    void placementChanged(const QString& moduleName, const QRect& pageRect, bool onScreen);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // EVERY ANCESTOR, because the surface's own move and resize events are not
    // enough: the chrome above it growing moves the page without this widget
    // moving inside its own parent, and a dock being re-tabbed reparents the
    // whole column. Re-taken on a parent change.
    void watchAncestors();
    void announce();

    QString m_moduleName;
    QVector<QPointer<QWidget>> m_watched;
    QRect m_lastRect;
    bool m_lastOnScreen = false;
    bool m_announced = false;
};
