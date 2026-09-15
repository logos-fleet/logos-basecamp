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
  var onCanvas = function (r) {
    var c = canvas();
    if (!c) return false;
    var box = c.getBoundingClientRect();
    return r.width >= 20 && r.height >= 20
        && r.top >= box.top && r.bottom <= box.bottom
        && r.left >= box.left && r.right <= box.right;
  };
  var reachable = function (el) { return onCanvas(el.getBoundingClientRect()); };

  // Exactly, then case-insensitively from the front. The second is what lets a
  // caller ask for 'Account label' when the field's accessible name is its
  // placeholder, 'Account label (e.g. main)'.
  var matches = function (el, name) {
    var own = nameOf(el);
    if (!own) return false;
    if (own === name) return true;
    return own.toLowerCase().indexOf(name.toLowerCase()) === 0;
  };

  // ── THE TWO HANDLES A CONTROL CAN HAVE ────────────────────────────────────
  //
  // 1. ITS objectName, asked of the bundled QML runtime
  //    (`logosViewItem` -> LogosWebRuntime::describeItem). It answers the rect
  //    in the WINDOW's coordinates and, for a field, the text the item now
  //    holds -- which is the module's own state and the only honest readback
  //    there is.
  // 2. ITS ACCESSIBLE NAME, off Qt's accessibility DOM. It is what a button
  //    has: a Logos button carries no objectName, and its text is its name.
  //
  // The first is tried first and the second is the fallback, because a page
  // publishes an EDITABLE TEXT ITEM WITH NO NAME AT ALL -- measured, Qt's wasm
  // accessibility writes `<input aria-hidden="true">` for one -- so the control
  // a typed flow is about is exactly the one the tree cannot be asked for.
  //
  // Both answers are normalised to CLIENT coordinates, which is what a pointer
  // event carries: the canvas's own box is the window's origin.
  var fromRuntime = function (name, done) {
    if (!window.logosWebViewReady) { done(null); return; }
    window.logosWebViewReady.then(function (state) {
      var item = null;
      try { item = JSON.parse(state.runtime.logosViewItem(state.module, name)); }
      catch (e) { item = null; }
      if (!item || !item.found) { done(null); return; }
      var c = canvas();
      if (!c) { done(null); return; }
      var box = c.getBoundingClientRect();
      done({ left: box.left + item.x, top: box.top + item.y,
             width: item.width, height: item.height,
             value: item.text, via: 'objectName' });
    }).catch(function () { done(null); });
  };
  var fromTree = function (name) {
    var all = controls(), named = [], reach = [];
    for (var i = 0; i < all.length; i++) {
      if (!matches(all[i], name)) continue;
      named.push(all[i]);
      if (reachable(all[i])) reach.push(all[i]);
    }
    var el = reach.length ? reach[0] : null;
    if (!el) return { el: null, onPage: named.length > 0 };
    var r = el.getBoundingClientRect();
    return { el: el, left: r.left, top: r.top, width: r.width, height: r.height,
             value: el.type === 'password'
                        ? String(el.value || '').length + ' character(s)'
                        : String(el.value === undefined ? '' : el.value),
             via: 'accessible name' };
  };
  // Every name the page CAN be asked for, for a refusal that names them.
  var reachableNames = function () {
    var all = controls(), here = [];
    for (var j = 0; j < all.length; j++) {
      if (nameOf(all[j]) && reachable(all[j])) here.push(nameOf(all[j]));
    }
    return here;
  };
  var find = function (name, done) {
    fromRuntime(name, function (item) {
      if (item) {
        if (!onCanvas({ left: item.left, top: item.top,
                        right: item.left + item.width, bottom: item.top + item.height,
                        width: item.width, height: item.height })) {
          say("'" + name + "' is on the page and not reachable");
          done(null);
          return;
        }
        done(item);
        return;
      }
      var found = fromTree(name);
      if (found.el) { done(found); return; }
      if (found.onPage) { say("'" + name + "' is on the page and not reachable"); }
      else {
        var here = reachableNames();
        say("no control named '" + name + "'; reachable: "
            + (here.length ? here.join(' | ') : '(nothing)'));
      }
      done(null);
    });
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
  var pressRect = function (r) {
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
  // and off the device with it; both handles answer a password field with its
  // length instead, and that text is what is said.
  var valueOf = function (found) {
    var v = found.value;
    if (v === undefined || v === null) return "''";
    if (/ character\(s\)$/.test(String(v))) return String(v);
    return "'" + String(v) + "'";
  };

  // EVERY ENTRY POINT WAKES THE TREE AND THEN WAITS A TURN. Waking it is a
  // click Qt answers on its own event loop, and a tree read in the same turn is
  // the empty one. Harmless for a control found by objectName, which does not
  // need the tree at all.
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
        find(name, function (found) {
          if (!found) return;
          pressRect(found);
          say("pressed '" + name + "' (by " + found.via + ")");
        });
      });
    },

    // THE PRESS AND THE KEYS CANNOT BE IN THE SAME TURN of the page's event
    // loop: Qt moves focus on ITS loop, so keys sent with the press land on
    // whatever had the focus before it. And the field is looked up AGAIN for
    // the readback, because what it now holds is a second question from the
    // one the press was aimed at.
    type: function (name, text) {
      afterWaking(function () {
        find(name, function (found) {
          if (!found) return;
          pressRect(found);
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
              find(name, function (again) {
                say("'" + name + "' now " + valueOf(again || found));
              });
            }, 400);
          }, 800);
        });
      });
    },

    read: function (name) {
      afterWaking(function () {
        find(name, function (found) {
          if (found) say("'" + name + "' now " + valueOf(found));
        });
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
