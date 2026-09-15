#include "ShellPopupDriver.h"

#include <QImage>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QRectF>
#include <QScopedPointer>
#include <QUrl>
#include <QVariant>
#include <QWidget>

namespace {

// The handle on the rectangle INSIDE each shape, which is what the pixel test
// is about and what is looked up again through the same find() every other pass
// uses. A popup that ended up somewhere this host cannot walk says so in the
// same words a missing control would.
const QLatin1String kFillName("popupPaintProbe.fill");

// Bright, opaque and nothing like the Shell's palette, so "the frame changed
// here" cannot be a theme colour landing on itself and so a photograph of the
// screen can be read without a colour picker.
const QLatin1String kProbeColour("#ff00ff");

// THE THREE SHAPES, because #187 is not about popups in general -- it is about
// the two chat_ui puts on screen, and a probe that only covers the easy one
// would report the Shell healthy.
//
//   plain Popup    the mechanism with everything else taken away
//   modal Dialog   what NewConversationDialog is: modal, dimmed, and centred on
//                  the scene's Overlay rather than on the item it came from
//                  (logos-design-system LogosDialog.qml)
//   Menu           what the "+" opens. A Menu resolves its own popup type and
//                  can ask the platform for a NATIVE one, which is a second way
//                  for it to leave the scene (QQuickMenuPrivate::resolvedPopupType)
//
// Each fills itself with the same rectangle under the same handle, so the same
// two questions are asked of all three.
struct Shape {
    QLatin1String name;
    const char*   qml;
};

const char kPlainPopupQml[] = R"(
import QtQuick
import QtQuick.Controls

Popup {
    objectName: "popupPaintProbe.popup"
    modal: false
    dim: false
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: 240
    height: 160
    background: Rectangle {
        objectName: "popupPaintProbe.fill"
        color: "#ff00ff"
        border.color: "#00ff00"
        border.width: 4
    }
}
)";

const char kModalDialogQml[] = R"(
import QtQuick
import QtQuick.Controls

Dialog {
    objectName: "popupPaintProbe.dialog"
    modal: true
    closePolicy: Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    padding: 0
    width: 240
    height: 160
    Overlay.modal: Rectangle { color: "#a0000000" }
    background: Rectangle {
        objectName: "popupPaintProbe.fill"
        color: "#ff00ff"
        border.color: "#00ff00"
        border.width: 4
    }
}
)";

const char kMenuQml[] = R"(
import QtQuick
import QtQuick.Controls

Menu {
    objectName: "popupPaintProbe.menu"
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: 240
    height: 160
    background: Rectangle {
        objectName: "popupPaintProbe.fill"
        color: "#ff00ff"
        border.color: "#00ff00"
        border.width: 4
    }
    MenuItem { objectName: "popupPaintProbe.menuItem"; text: "probe" }
}
)";

const Shape kShapes[] = {
    { QLatin1String("plain Popup"),  kPlainPopupQml   },
    { QLatin1String("modal Dialog"), kModalDialogQml  },
    { QLatin1String("Menu"),         kMenuQml         },
};

// How long a shape gets to be instantiated, laid out and rendered before the
// frame is read. The enter transitions are the styles' own, so this is a
// polish pass, a transition and a render -- measured well under 200 ms on the
// iPad Air simulator, with room here for a device under load.
constexpr int kOpenSettleMs = 700;
// And how long each one is left standing once its verdict is printed, so a
// screenshot taken from the host has something to photograph. The same round
// trip the keyboard pass leaves for the same reason.
constexpr int kHoldMs = 3000;
// And what a close is given before the next shape's `before` frame is grabbed,
// so the one that just went away is not counted as the next one appearing.
constexpr int kCloseSettleMs = 200;

QString popupTypeName(const QVariant& value)
{
    // Popup.Item / Popup.Window / Popup.Native, whose numbers are Qt's. Named
    // because #187's first hypothesis is that this is Window -- a popup in a
    // window of its own, which a QQuickWidget renders into no screen at all.
    switch (value.toInt()) {
    case 0:  return QStringLiteral("Item");
    case 1:  return QStringLiteral("Window");
    case 2:  return QStringLiteral("Native");
    default: return QStringLiteral("unknown(%1)").arg(value.toInt());
    }
}

} // namespace

ShellPopupDriver::ShellPopupDriver(QWidget* shellWidget, QObject* parent)
    : ShellSceneDriver(shellWidget, parent)
{
}

void ShellPopupDriver::run()
{
    if (!m_shell) {
        emit log(QStringLiteral("popups: no shell widget"));
        return;
    }
    // Every scene the Shell has on screen. The Shell lays itself out in several
    // QQuickWidgets (MainContainer: the sidebar, the content stack, the dialog
    // overlay) and a mounted app is one more -- and "the app's popups are
    // missing" and "the Shell's own are too" are different bugs, so each is
    // asked separately rather than one standing in for all.
    int probed = 0;
    for (QQuickWidget* surface : m_shell->findChildren<QQuickWidget*>()) {
        // The same rule every lookup in this host follows: a scene nobody can
        // see is not one to ask about (basecamp::shell::sceneIsOnScreen).
        if (!basecamp::shell::sceneIsOnScreen(surface))
            continue;
        if (probe(surface))
            ++probed;
    }
    if (probed == 0)
        emit log(QStringLiteral("popups: no scene in this Shell could be probed"));
}

bool ShellPopupDriver::probe(QQuickWidget* surface)
{
    QQuickItem* root = surface->rootObject();
    const QString file = surface->source().fileName();
    const QString scene = file.isEmpty() ? QStringLiteral("<no source>") : file;
    if (!root || !surface->engine()) {
        emit log(QStringLiteral("popups: scene '%1' has no root object to put a popup in")
                     .arg(scene));
        return false;
    }
    emit log(QStringLiteral("popups: scene '%1' is %2x%3")
                 .arg(scene).arg(surface->width()).arg(surface->height()));

    // Every shape, whatever the one before it answered -- "the plain Popup
    // draws and the Menu does not" is the whole diagnosis, and stopping at the
    // first failure would hide half of it.
    bool any = false;
    for (const Shape& shape : kShapes) {
        if (probeShape(surface, scene, shape.name, shape.qml))
            any = true;
    }
    return any;
}

bool ShellPopupDriver::probeShape(QQuickWidget* surface, const QString& scene,
                                  const QString& shape, const char* qml)
{
    QQmlComponent component(surface->engine());
    component.setData(QByteArray(qml), QUrl());
    if (component.isError()) {
        emit log(QStringLiteral("popups: the %1 probe would not compile in '%2': %3")
                     .arg(shape, scene, component.errorString().trimmed()));
        return false;
    }
    QScopedPointer<QObject> popup(
        component.createWithInitialProperties(
            { { QStringLiteral("parent"), QVariant::fromValue(surface->rootObject()) } }));
    if (!popup) {
        emit log(QStringLiteral("popups: the %1 probe would not instantiate in '%2': %3")
                     .arg(shape, scene, component.errorString().trimmed()));
        return false;
    }

    // Middle of the surface, so the answer is never "it is off the viewport".
    // The modal Dialog ignores this -- it is centred on the Overlay, which is
    // the whole point of that shape -- and everything below reads the geometry
    // back off the scene rather than trusting what was asked for.
    const qreal width  = popup->property("width").toReal();
    const qreal height = popup->property("height").toReal();
    popup->setProperty("x", (surface->width() - width) / 2.0);
    popup->setProperty("y", (surface->height() - height) / 2.0);

    const QImage before = frameOf(surface);
    QMetaObject::invokeMethod(popup.data(), "open");
    settle(kOpenSettleMs);
    const QImage after = frameOf(surface);

    // ── what the scene says ──
    // The fill, looked up the way every other pass looks a control up. Finding
    // it is itself a fact: a popup that went into a window of its own is in no
    // scene this host walks, and find() answers nothing for it.
    QQuickItem* fill = find(kFillName, Scope::WithOverlays);
    const bool here = fill && fill->window() == surface->quickWindow();
    emit log(QStringLiteral("popups: %1 in '%2': popupType=%3, opened=%4, in this scene: %5")
                 .arg(shape, scene,
                      popupTypeName(popup->property("popupType")),
                      popup->property("opened").toBool() ? QStringLiteral("true")
                                                         : QStringLiteral("false"),
                      here ? QStringLiteral("yes")
                           : (fill ? QStringLiteral("no -- in another window")
                                   : QStringLiteral("no -- in no scene at all"))));

    if (!here) {
        emit log(QStringLiteral("WRONG: the %1 in '%2' is not in the scene this surface "
                                "renders -- nothing it draws can reach the screen")
                     .arg(shape, scene));
        QMetaObject::invokeMethod(popup.data(), "close");
        settle(kCloseSettleMs);
        return true;
    }

    // ── what the frame says ──
    // Measured and reported at the FILL'S OWN rectangle, not the one asked for
    // above: a dialog centred on the Overlay and a menu positioned by the style
    // both decide where they are, and a pixel test aimed anywhere else would be
    // measuring the background. `here` means the fill is in this surface's
    // scene, so pixelsChangedUnder -- which finds the surface from the item --
    // reads the two frames grabbed above. `looked` is 0 when there was no pair
    // of frames to compare at all, which is not "not one pixel changed".
    const QRectF where(fill->mapToScene(QPointF(0, 0)),
                       QSizeF(fill->width(), fill->height()));
    int looked = 0;
    const int changed = pixelsChangedUnder(fill, before, after, &looked);
    const QPointF onScreen = surface->mapToGlobal(where.topLeft());
    if (looked == 0) {
        emit log(QStringLiteral("popups: '%1' rendered no frame to compare "
                                "(grab %2x%3) -- nothing can be said about the %4")
                     .arg(scene).arg(before.width()).arg(before.height()).arg(shape));
    } else if (changed == 0) {
        emit log(QStringLiteral("WRONG: '%1' draws no %2 -- a %3x%4 %5 rectangle is open "
                                "at (%6, %7) in the scene and not one of the %8 pixels "
                                "under it changed in the surface's frame")
                     .arg(scene, shape)
                     .arg(where.width(), 0, 'f', 0).arg(where.height(), 0, 'f', 0)
                     .arg(kProbeColour)
                     .arg(where.x(), 0, 'f', 0).arg(where.y(), 0, 'f', 0)
                     .arg(looked));
    } else {
        emit log(QStringLiteral("popups: '%1' PAINTS the %2 -- %3 of %4 pixels changed "
                                "under it")
                     .arg(scene, shape).arg(changed).arg(looked));
        emit log(QStringLiteral("popups: it is at (%1, %2) %3x%4 on the screen; holding it "
                                "for %5 ms so a screenshot can see it")
                     .arg(onScreen.x(), 0, 'f', 0).arg(onScreen.y(), 0, 'f', 0)
                     .arg(where.width(), 0, 'f', 0).arg(where.height(), 0, 'f', 0)
                     .arg(kHoldMs));
        settle(kHoldMs);
    }

    QMetaObject::invokeMethod(popup.data(), "close");
    settle(kCloseSettleMs);
    return true;
}
