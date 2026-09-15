#include "webview/WebPageInput.h"

#include <QLatin1String>
#include <QStringList>

namespace basecamp::web {

namespace {

const QLatin1String kMarker("logos-drive: ");

// A JavaScript string literal. Single quotes, because the answers quote the
// control's name back in single quotes and a driver reads them that way.
QString quoted(const QString& text)
{
    QString out = text;
    out.replace(QLatin1String("\\"), QLatin1String("\\\\"));
    out.replace(QLatin1String("'"), QLatin1String("\\'"));
    out.replace(QLatin1String("\n"), QLatin1String("\\n"));
    out.replace(QLatin1String("\r"), QString());
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

// "'<control>' now <rest>" -- where the rest starts, or -1.
int afterNow(const QString& line, const QString& control)
{
    const QString head = QStringLiteral("'%1' now ").arg(control);
    const int at = line.indexOf(head);
    return at < 0 ? -1 : at + head.size();
}

} // namespace

QString WebPageInput::marker()
{
    return kMarker;
}

QString WebPageInput::driverScript()
{
    // The page's half. Every finding baked into it is stated where it is used;
    // WebPageInput.h says why any of it exists.
    return QStringLiteral(R"JS(
(function () {
  if (window.logosDrive) return;
  var say = function (m) { console.log('logos-drive: ' + m); };

  var root = function () {
    var host = document.querySelector('#qt-shadow-container');
    return (host && host.shadowRoot) || document;
  };
  var canvas = function () { return root().querySelector('canvas'); };

  // Qt builds its accessibility DOM only once a screen-reader user has asked
  // for it, through the hidden button it leaves in the window's container. The
  // button is REMOVED once it has been pressed, so "there is no button" is the
  // ordinary state of a woken page rather than a failure.
  var wake = function () {
    var button = root().querySelector('.hidden-visually-read-by-screen-reader');
    if (button) button.click();
  };
  var controls = function () {
    var box = root().querySelector('.qt-window-a11y-container');
    return box ? Array.prototype.slice.call(box.querySelectorAll('*')) : [];
  };
  var nameOf = function (el) { return el.getAttribute('aria-label') || ''; };

  // A control a finger could actually hit. Qt gives every item on a page that
  // is NOT showing a token 16x6 rect at the window's origin, and a press at one
  // of those lands in the corner of the scene and looks like a platform that
  // swallows events. An item below the fold is reported for what it is rather
  // than pressed: this driver does not scroll, and a form whose fields are off
  // the page is a finding, not something to paper over.
  var reachable = function (el) {
    var r = el.getBoundingClientRect();
    if (r.width < 20 || r.height < 20) return false;
    var c = canvas();
    if (!c) return false;
    var box = c.getBoundingClientRect();
    return r.top >= box.top && r.bottom <= box.bottom
        && r.left >= box.left && r.right <= box.right;
  };

  // Exactly, then case-insensitively from the front. The second is what lets a
  // caller ask for 'Account label' when the field's accessible name is its
  // placeholder, 'Account label (e.g. main)'.
  var matches = function (el, name) {
    var own = nameOf(el);
    if (!own) return false;
    if (own === name) return true;
    return own.toLowerCase().indexOf(name.toLowerCase()) === 0;
  };
  var find = function (name) {
    var all = controls(), named = [], reach = [];
    for (var i = 0; i < all.length; i++) {
      if (!matches(all[i], name)) continue;
      named.push(all[i]);
      if (reachable(all[i])) reach.push(all[i]);
    }
    if (reach.length) return reach[0];
    if (named.length) { say("'" + name + "' is on the page and not reachable"); return null; }
    var here = [];
    for (var j = 0; j < all.length; j++) {
      if (nameOf(all[j]) && reachable(all[j])) here.push(nameOf(all[j]));
    }
    say("no control named '" + name + "'; reachable: "
        + (here.length ? here.join(' | ') : '(nothing)'));
    return null;
  };

  // Qt captures the pointer on pointerdown, and a browser refuses to capture an
  // id no real input device owns -- which throws out of the dispatch and leaves
  // the press half delivered.
  var stub = function () {
    if (Element.prototype.__logosCaptureStubbed) return;
    Element.prototype.setPointerCapture = function () {};
    Element.prototype.releasePointerCapture = function () {};
    Element.prototype.__logosCaptureStubbed = true;
  };

  // WHAT clientX HAS TO BE FOR QT TO SEE THE POINT WE MEAN. A Qt wasm window
  // takes its local point off the event's offsetX/offsetY, those cannot be set
  // (a PointerEventInit has no field for them and the browser derives them from
  // the target's box), and the derivation is NOT the same on both engines --
  // measured, an Android WebView reports clientX/devicePixelRatio while
  // WKWebView reports clientX itself. Two probe moves measure the line instead
  // of believing either. A pointermove is free: Qt treats it as a hover.
  var calibrate = function (target, box) {
    var probe = function (x, y) {
      var e = new PointerEvent('pointermove', { bubbles: true, cancelable: true,
        composed: true, clientX: x, clientY: y, pointerId: 1, pointerType: 'mouse',
        isPrimary: true, button: -1, buttons: 0 });
      target.dispatchEvent(e);
      return { x: e.offsetX, y: e.offsetY };
    };
    var a = probe(box.left, box.top);
    var b = probe(box.left + 100, box.top + 100);
    var sx = (b.x - a.x) / 100, sy = (b.y - a.y) / 100;
    if (!sx || !sy) return null;
    return { sx: sx, sy: sy, bx: a.x - sx * box.left, by: a.y - sy * box.top };
  };
  var pressAt = function (x, y, kind) {
    var c = canvas();
    if (!c) { say('this page has no canvas to press'); return; }
    var box = c.getBoundingClientRect();
    var target = (root().elementFromPoint ? root().elementFromPoint(x, y) : null) || c;
    var line = calibrate(target, box);
    var cx = x, cy = y;
    if (line) { cx = (x - line.bx) / line.sx; cy = (y - line.by) / line.sy; }
    stub();
    var send = function (type, extra) {
      var init = { bubbles: true, cancelable: true, composed: true,
        clientX: cx, clientY: cy, screenX: cx, screenY: cy,
        pointerId: kind === 'touch' ? 2 : 1, pointerType: kind, isPrimary: true,
        width: 4, height: 4, pressure: 0.5 };
      for (var k in extra) init[k] = extra[k];
      target.dispatchEvent(new PointerEvent(type, init));
    };
    send('pointermove', { button: -1, buttons: 0 });
    send('pointerdown', { button: 0, buttons: 1 });
    send('pointerup', { button: 0, buttons: 0, pressure: 0 });
  };
  // ...AND THE SAME PRESS AS A FINGER, a moment later: the two are separate
  // paths through Qt and a webview may act on one and not the other.
  var pressElement = function (el) {
    var r = el.getBoundingClientRect();
    var x = r.left + r.width / 2, y = r.top + r.height / 2;
    pressAt(x, y, 'mouse');
    setTimeout(function () { pressAt(x, y, 'touch'); }, 250);
  };

  // Qt does not read keys off the canvas when a text item has focus: it focuses
  // a hidden input so that a phone raises its keyboard and an IME has somewhere
  // to compose. The DEEPEST active element is therefore the only target that
  // works for both, and it is what a real keystroke would have reached anyway.
  var deepActive = function () {
    var n = document.activeElement;
    while (n && n.shadowRoot && n.shadowRoot.activeElement) n = n.shadowRoot.activeElement;
    return n;
  };
  // A PASSPHRASE IS NOT PRINTED. The page's console crosses to the app's log
  // and off the device with it; the length is enough to say the keys arrived.
  var valueOf = function (el) {
    var v = (el.value === undefined || el.value === null) ? '' : String(el.value);
    if (el.type === 'password') return v.length + ' character(s)';
    return "'" + v + "'";
  };

  // EVERY ENTRY POINT WAKES THE TREE AND THEN WAITS A TURN. Waking it is a
  // click Qt answers on its own event loop, and a tree read in the same turn is
  // the empty one.
  var afterWaking = function (work) { wake(); setTimeout(work, 300); };

  window.logosDrive = {
    describe: function () {
      afterWaking(function () {
        var all = controls(), lines = [];
        for (var i = 0; i < all.length; i++) {
          if (!reachable(all[i])) continue;
          var r = all[i].getBoundingClientRect();
          lines.push(all[i].tagName.toLowerCase()
                     + (all[i].type ? '[' + all[i].type + ']' : '')
                     + " '" + nameOf(all[i]) + "' at "
                     + Math.round(r.left) + ',' + Math.round(r.top));
        }
        if (!all.length) {
          say('the accessibility tree is empty -- this page published no controls');
          return;
        }
        say(lines.length + ' reachable control(s) of ' + all.length + ': ' + lines.join(' | '));
      });
    },

    press: function (name) {
      afterWaking(function () {
        var el = find(name);
        if (!el) return;
        pressElement(el);
        say("pressed '" + name + "'");
      });
    },

    // THE PRESS AND THE KEYS CANNOT BE IN THE SAME TURN of the page's event
    // loop: Qt moves focus on ITS loop, so keys sent with the press land on
    // whatever had the focus before it. And the field is looked up AGAIN for
    // the readback, because Qt rebuilds an accessibility element when the item
    // it mirrors changes.
    type: function (name, text) {
      afterWaking(function () {
        var el = find(name);
        if (!el) return;
        pressElement(el);
        setTimeout(function () {
          var target = deepActive() || document.body;
          for (var i = 0; i < text.length; i++) {
            var key = text[i];
            var init = { key: key, code: 'Key' + key.toUpperCase(),
                         bubbles: true, cancelable: true, composed: true };
            target.dispatchEvent(new KeyboardEvent('keydown', init));
            target.dispatchEvent(new KeyboardEvent('keyup', init));
          }
          setTimeout(function () {
            var again = find(name) || el;
            say("'" + name + "' now " + valueOf(again));
          }, 400);
        }, 800);
      });
    },

    read: function (name) {
      afterWaking(function () {
        var el = find(name);
        if (el) say("'" + name + "' now " + valueOf(el));
      });
    }
  };
  say('driver installed');
})();
)JS");
}

QString WebPageInput::describeCall()
{
    return QStringLiteral("window.logosDrive.describe()");
}

QString WebPageInput::pressCall(const QString& control)
{
    return QStringLiteral("window.logosDrive.press(%1)").arg(quoted(control));
}

QString WebPageInput::typeCall(const QString& control, const QString& text)
{
    return QStringLiteral("window.logosDrive.type(%1, %2)")
        .arg(quoted(control), quoted(text));
}

QString WebPageInput::readCall(const QString& control)
{
    return QStringLiteral("window.logosDrive.read(%1)").arg(quoted(control));
}

std::optional<QString> WebPageInput::valueReported(const QString& line, const QString& control)
{
    if (!line.contains(kMarker)) return std::nullopt;
    const int at = afterNow(line, control);
    if (at < 0) return std::nullopt;
    const QString rest = line.mid(at).trimmed();
    // A text field answers with its contents in quotes; a password field
    // answers with a count, and that text IS the answer -- a caller comparing
    // it against what it typed will not match, which is the honest outcome for
    // a field whose contents are deliberately not readable.
    if (rest.startsWith(QLatin1Char('\'')) && rest.endsWith(QLatin1Char('\''))
        && rest.size() >= 2)
        return rest.mid(1, rest.size() - 2);
    return rest;
}

bool WebPageInput::pressReported(const QString& line, const QString& control)
{
    return line.contains(kMarker)
        && line.contains(QStringLiteral("pressed '%1'").arg(control));
}

bool WebPageInput::refusalReported(const QString& line, const QString& control)
{
    if (!line.contains(kMarker)) return false;
    return line.contains(QStringLiteral("no control named '%1'").arg(control))
        || line.contains(QStringLiteral("'%1' is on the page and not reachable").arg(control));
}

} // namespace basecamp::web
