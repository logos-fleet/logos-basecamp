#include "WebModuleRunner.h"

#include "webview/AppMemory.h"
#include "webview/MobileWebContainerBackend.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

namespace {

// ── driving real input at a canvas ─────────────────────────────────────────
//
// A `web` variant's view is pixels in a canvas: there is no DOM node to touch,
// no text node to read and no accessibility tree to drive. So a host that has
// to show that a real key and a real finger reach the view dispatches the
// events IN THE PAGE, and then waits for the module's own QML to say what
// arrived. Everything asserted afterwards is the module reporting on itself.
//
// The same two gestures the browser end-to-end drives
// (logos-module-builder's wasm/browser-e2e), for the same reason and with the
// same two findings baked in:
//
//   * THE KEY EVENT ALONE. Qt takes the character off `keydown`; adding the
//     `input` event a real IME would also fire types every character twice.
//   * POINTER CAPTURE HAS TO BE DEFUSED. Qt captures the pointer on
//     pointerdown and a browser refuses to capture an id no real input device
//     owns -- which throws out of the dispatch and leaves a drag half
//     delivered.
//
// And one that is this venue's own: the press and the keys cannot be in the
// same turn of the page's event loop. Qt's focus change happens on ITS event
// loop, so the keys are sent from a timer, after the click has landed.
const char* kInputDriver = R"JS(
(function () {
  if (window.logosDrive) return;
  var canvas = function () {
    var host = document.querySelector('#qt-shadow-container');
    var root = (host && host.shadowRoot) || document;
    return { root: root, el: root.querySelector('canvas') };
  };
  var stub = function () {
    if (Element.prototype.__logosCaptureStubbed) return;
    Element.prototype.setPointerCapture = function () {};
    Element.prototype.releasePointerCapture = function () {};
    Element.prototype.__logosCaptureStubbed = true;
  };
  var pointAt = function (x, y) {
    var c = canvas();
    if (!c.el) return null;
    var rect = c.el.getBoundingClientRect();
    var cx = rect.left + x, cy = rect.top + y;
    var target = (c.root.elementFromPoint ? c.root.elementFromPoint(cx, cy) : null) || c.el;
    // SAID OUT LOUD, because a pointer event that lands on the wrong element is
    // indistinguishable from one that was never dispatched: both are silence.
    // The canvas rect and the device pixel ratio are the two numbers that
    // decide whether the view's own coordinates mean what this assumes.
    var chain = '', node = target;
    for (var i = 0; i < 4 && node; i++) {
      chain += (i ? ' < ' : '') + node.tagName
               + (node.className ? '.' + String(node.className) : '');
      node = node.parentElement;
    }
    var dpr = window.devicePixelRatio || 1;
    console.log('logos-drive: at ' + cx + ',' + cy + ' (device ' + Math.round(cx * dpr)
                + ',' + Math.round(cy * dpr) + ') hit ' + chain
                + ' canvas ' + Math.round(rect.width) + 'x' + Math.round(rect.height)
                + ' dpr ' + dpr);
    return { target: target, x: cx, y: cy };
  };
  var send = function (target, type, x, y, kind, extra) {
    var init = { bubbles: true, cancelable: true, composed: true,
                 clientX: x, clientY: y, screenX: x, screenY: y,
                 pointerId: kind === 'touch' ? 2 : 1, pointerType: kind,
                 isPrimary: true, width: 4, height: 4, pressure: 0.5 };
    for (var k in extra) init[k] = extra[k];
    var event = new PointerEvent(type, init);
    // offsetX/offsetY, EXPLICITLY, and they are the coordinates Qt actually
    // reads: a wasm window takes its local point off the event's offset, not
    // off clientX. A PointerEventInit cannot carry them -- the browser is
    // supposed to derive them from the target's box -- and a derivation that
    // comes out as 0,0 is a press in the window's top-left corner, which is a
    // press on nothing with no error anywhere.
    var box = target.getBoundingClientRect();
    Object.defineProperty(event, 'offsetX', { get: function () { return x - box.left; } });
    Object.defineProperty(event, 'offsetY', { get: function () { return y - box.top; } });
    var accepted = !target.dispatchEvent(event);
    if (type === 'pointerdown')
      console.log('logos-drive: ' + kind + ' press at ' + Math.round(event.offsetX)
                  + ',' + Math.round(event.offsetY) + ' taken ' + accepted);
  };
  var deepActive = function () {
    var node = document.activeElement;
    while (node && node.shadowRoot && node.shadowRoot.activeElement)
      node = node.shadowRoot.activeElement;
    return node;
  };

  window.logosDrive = {
    // A TAP, ON ITS OWN. Diagnosis as much as assertion: a tap whose effect the
    // module reports is the shortest proof that this platform's webview
    // delivers a pointer event to Qt AT ALL, and every richer gesture below is
    // only worth reading once that holds.
    tap: function (x, y) {
      var p = pointAt(x, y);
      if (!p) { console.log('logos-drive: no canvas'); return; }
      stub();
      // FOUR TIMES, TWO WAYS, SPREAD OVER A SECOND. A tap is the one gesture
      // that has to work before any other is worth reading, and what a phone's
      // webview needs to deliver one differs: `mouse` and `touch` are separate
      // paths through Qt, and a window that is not yet active spends the first
      // press activating. Sending the sequence more than once costs a second
      // and removes both questions -- the module reports once, however many
      // presses it took.
      var round = 0;
      var press = function () {
        var kind = (round % 2) ? 'touch' : 'mouse';
        send(p.target, 'pointerover', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointerenter', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointermove', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointerdown', p.x, p.y, kind, { button: 0, buttons: 1 });
        send(p.target, 'pointerup', p.x, p.y, kind, { button: 0, buttons: 0, pressure: 0 });
        if (++round < 4) setTimeout(press, 300);
      };
      press();
    },

    type: function (x, y, text) {
      var p = pointAt(x, y);
      if (!p) { console.log('logos-drive: no canvas'); return; }
      stub();
      // The same press sequence tap() sends, for the same reasons.
      var round = 0;
      var press = function () {
        var kind = (round % 2) ? 'touch' : 'mouse';
        send(p.target, 'pointerover', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointerenter', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointermove', p.x, p.y, kind, { button: -1, buttons: 0 });
        send(p.target, 'pointerdown', p.x, p.y, kind, { button: 0, buttons: 1 });
        send(p.target, 'pointerup', p.x, p.y, kind, { button: 0, buttons: 0, pressure: 0 });
        if (++round < 4) setTimeout(press, 300);
      };
      press();
      // AFTER the presses have been processed: Qt moves focus on its own event
      // loop, and keys sent in this turn would land on whatever had it before.
      setTimeout(function () {
        var target = deepActive() || document.body;
        console.log('logos-drive: keys to ' + target.tagName);
        for (var i = 0; i < text.length; i++) {
          var key = text[i];
          var init = { key: key, code: 'Key' + key.toUpperCase(),
                       bubbles: true, cancelable: true, composed: true };
          target.dispatchEvent(new KeyboardEvent('keydown', init));
          target.dispatchEvent(new KeyboardEvent('keyup', init));
        }
      }, 1800);
    },

    drag: function (x, y, dy) {
      var p = pointAt(x, y);
      if (!p) { console.log('logos-drive: no canvas'); return; }
      stub();
      send(p.target, 'pointerdown', p.x, p.y, 'touch', { button: 0, buttons: 1 });
      var step = 0;
      var move = function () {
        step++;
        send(p.target, 'pointermove', p.x, p.y + (dy * step) / 8, 'touch',
             { button: -1, buttons: 1 });
        // ONE FRAME APART. Flickable has no velocity from a single sample, and
        // eight moves delivered in one turn of the loop are one teleport.
        if (step < 8) { setTimeout(move, 16); return; }
        setTimeout(function () {
          send(p.target, 'pointerup', p.x, p.y + dy, 'touch',
               { button: 0, buttons: 0, pressure: 0 });
        }, 16);
      };
      setTimeout(move, 16);
    }
  };
})();
)JS";

} // namespace

using basecamp::web::megabytes;
using basecamp::web::MobileWebContainerBackend;

WebModuleRunner::WebModuleRunner(ICoreRuntime* core, QString webModulesDir, QObject* parent)
    : QObject(parent)
    , m_core(core)
    , m_webModulesDir(std::move(webModulesDir))
{
    connect(MobileWebContainerBackend::instance(), &MobileWebContainerBackend::pageLog,
            this, [this](const QString& module, const QString& level, const QString& message) {
                m_pageLines.append(message);
                // Only what the MODULE says, not every line a 25 MB runtime
                // emits while booting: the log is read off a device console and
                // the interesting lines would be lost in it.
                // `contains`, not `startsWith`: Qt's own console route prefixes
                // a page's qml log with "qml: ".
                if (message.contains(QStringLiteral("logos-view:"))
                    || message.contains(QStringLiteral("logos-drive:"))
                    || message.contains(QStringLiteral("[logos-web-view"))
                    || message.contains(QStringLiteral("[logos-web-host"))
                    || level == QLatin1String("error"))
                    emit log(QStringLiteral("web %1: %2").arg(module, message));
            });
}

void WebModuleRunner::pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool WebModuleRunner::waitForPageLine(const QString& pattern, int timeoutMs)
{
    const QRegularExpression re(pattern);
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        for (const QString& line : m_pageLines)
            if (re.match(line).hasMatch()) return true;
        if (timer.elapsed() >= timeoutMs) return false;
        pump(100);
    }
}

QStringList WebModuleRunner::capturePageLine(const QString& pattern, int timeoutMs)
{
    const QRegularExpression re(pattern);
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        // BACKWARDS. Every one of these lines is reported on a repeating timer
        // by the fixture, so the list holds many; the last is the one that
        // describes the view as it is NOW.
        for (int i = m_pageLines.size() - 1; i >= 0; --i) {
            const QRegularExpressionMatch match = re.match(m_pageLines.at(i));
            if (match.hasMatch()) return match.capturedTexts();
        }
        if (timer.elapsed() >= timeoutMs) return {};
        pump(100);
    }
}

// ── the first criterion's second half ──────────────────────────────────────

bool WebModuleRunner::tapButton(const QString& name)
{
    auto* backend = MobileWebContainerBackend::instance();
    const QStringList at = capturePageLine(
        QStringLiteral("logos-view: button-at ([1-9][0-9]*) ([1-9][0-9]*)"), 20000);
    if (at.isEmpty()) return false;

    if (!backend->runJavaScriptIn(name, QString::fromUtf8(kInputDriver))
        || !backend->runJavaScriptIn(name, QStringLiteral("window.logosDrive.tap(%1, %2)")
                                               .arg(at.at(1), at.at(2)))) {
        emit log(QStringLiteral("web module %1: this platform cannot put an event in "
                                "the page").arg(name));
        return false;
    }

    // THE WHOLE ROUND TRIP, FROM ONE TAP: the view's button calls the backend
    // over the MessagePort, the backend's property changes, and the change
    // comes back and rebinds the view -- which is what the fixture reports.
    const bool counted = waitForPageLine(
        QStringLiteral("logos-view: changed %1 count=[1-9]").arg(name), 20000);
    emit log(counted
                 ? QStringLiteral("POINTER: a tap on %1's button drove its backend and the "
                                  "change came back to the view").arg(name)
                 : QStringLiteral("WRONG: a tap on %1's button changed nothing").arg(name));
    return counted;
}

bool WebModuleRunner::typeIntoView(const QString& name)
{
    auto* backend = MobileWebContainerBackend::instance();
    const QStringList at = capturePageLine(
        QStringLiteral("logos-view: field-at ([1-9][0-9]*) ([1-9][0-9]*)"), 20000);
    if (at.isEmpty()) {
        emit log(QStringLiteral("web module %1: its view never reported a text field")
                     .arg(name));
        return false;
    }

    if (!backend->runJavaScriptIn(name, QString::fromUtf8(kInputDriver))
        || !backend->runJavaScriptIn(
            name, QStringLiteral("window.logosDrive.type(%1, %2, 'logos')")
                      .arg(at.at(1), at.at(2)))) {
        emit log(QStringLiteral("web module %1: this platform cannot put an event in "
                                "the page").arg(name));
        return false;
    }

    // WHAT THE MODULE SAYS IT GOT. `logos-view: typed logos` is the fixture's
    // own TextField reporting its contents after five keydowns -- the fact a
    // screenshot of a canvas cannot establish, and the fact a driver that
    // merely dispatched events cannot claim.
    const bool typed = waitForPageLine(QStringLiteral("logos-view: typed logos"), 20000);
    const QStringList focus = capturePageLine(
        QStringLiteral("logos-view: field-focus (true|false)"), 1000);
    emit log(typed
                 ? QStringLiteral("KEYBOARD: %1's text field has `logos` in it, typed by "
                                  "five key events at its editor (focus=%2)")
                       .arg(name, focus.size() > 1 ? focus.at(1) : QStringLiteral("?"))
                 : QStringLiteral("WRONG: %1's text field never took the keys").arg(name));
    return typed;
}

bool WebModuleRunner::scrollViewList(const QString& name)
{
    auto* backend = MobileWebContainerBackend::instance();
    const QStringList at = capturePageLine(
        QStringLiteral("logos-view: list-at ([1-9][0-9]*) ([1-9][0-9]*)"), 20000);
    if (at.isEmpty()) {
        emit log(QStringLiteral("web module %1: its view never reported a list").arg(name));
        return false;
    }

    if (!backend->runJavaScriptIn(name, QString::fromUtf8(kInputDriver))
        || !backend->runJavaScriptIn(
            name, QStringLiteral("window.logosDrive.drag(%1, %2, -90)")
                      .arg(at.at(1), at.at(2)))) {
        emit log(QStringLiteral("web module %1: this platform cannot put a gesture in "
                                "the page").arg(name));
        return false;
    }

    // THE ROW, not only the offset. A list that moved its content without
    // rebinding a delegate has not scrolled in any sense a user would name, so
    // the fixture reports the index it put at the top and this waits for one
    // that is not the first.
    const bool scrolled = waitForPageLine(
        QStringLiteral("logos-view: scrolled contentY=[1-9][0-9]* first=[1-9]"), 20000);
    const QStringList where = capturePageLine(
        QStringLiteral("logos-view: scrolled contentY=(\\d+) first=(-?\\d+)"), 1000);
    emit log(scrolled
                 ? QStringLiteral("SCROLLING: %1's list followed a drag to contentY=%2 "
                                  "with row %3 at the top")
                       .arg(name, where.value(1), where.value(2))
                 : QStringLiteral("WRONG: %1's list did not follow the drag (%2)")
                       .arg(name, where.isEmpty() ? QStringLiteral("no scroll line")
                                                  : where.at(0)));
    return scrolled;
}

// ── the third criterion ────────────────────────────────────────────────────

bool WebModuleRunner::callIntoPage(const QString& name, QString* answer)
{
    auto* backend = MobileWebContainerBackend::instance();

    // A `Methods` FRAME, AND IT IS THE CONTAINER'S OWN QUESTION. liblogos'
    // WebContainer decides a `web` module is serving by asking its page for its
    // interface and getting an answer (WebContainer::awaitLoad), and the wasm
    // host answers it off the live backend QObject's metaobject. So a
    // MethodsResult coming back here is not a proxy for "the module is
    // answering" -- it is the same evidence the container itself uses, taken
    // while the module's UI page is gone.
    //
    // A Call would be the more obvious frame and would prove less: a `ui_qml`
    // module's backend is reached by a QtRO replica, so its wasm host refuses
    // Calls on this transport BY DESIGN and a refusal would be ambiguous.
    const int id = 990001;
    const QString frame =
        QStringLiteral("{\"type\":7,\"payload\":{\"id\":%1,\"authToken\":\"\","
                       "\"object\":\"%2\"}}")
            .arg(QString::number(id), name);

    QString reply;
    backend->observeFramesFrom(name, [&reply, id](const QString& text) {
        if (!reply.isEmpty()) return;
        const QJsonObject envelope = QJsonDocument::fromJson(text.toUtf8()).object();
        // ONLY OUR ANSWER. The page is talking to the core on this wire at the
        // same time; everything that is not a MethodsResult carrying the id we
        // minted belongs to someone else.
        if (envelope.value(QStringLiteral("type")).toInt() != 8) return;
        const QJsonObject payload = envelope.value(QStringLiteral("payload")).toObject();
        if (payload.value(QStringLiteral("id")).toInt() != id) return;
        reply = text;
    });
    if (!backend->sendFrameTo(name, frame)) {
        backend->observeFramesFrom(name, nullptr);
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (reply.isEmpty() && timer.elapsed() < 20000) pump(100);
    backend->observeFramesFrom(name, nullptr);
    if (answer) *answer = reply;
    return !reply.isEmpty();
}

QStringList WebModuleRunner::available() const
{
    if (m_webModulesDir.isEmpty()) return {};
    const QStringList known = m_core->knownModules();
    QStringList out;
    for (const QString& name :
         QDir(m_webModulesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (known.contains(name)) out.append(name);
    }
    return out;
}

qint64 WebModuleRunner::bringUp(const QString& name)
{
    auto* backend = MobileWebContainerBackend::instance();

    QElapsedTimer timer;
    timer.start();
    // THE CONTAINER'S OWN VERDICT. liblogos' WebContainer returns from a load
    // only once it has asked the page for its interface and the page has
    // answered, so a true here already means both wasm images booted and the
    // module is published.
    if (!m_core->loadModule(name)) {
        emit log(QStringLiteral("web module %1: the core did not load it").arg(name));
        return -1;
    }
    emit log(QStringLiteral("web module %1: loaded and published in %2 ms")
                 .arg(name).arg(timer.elapsed()));

    // The shell says what the user is looking at. This is what brings the page
    // in front of Qt's own surface and what charges it against the budget.
    backend->show(name);
    if (!backend->hasView(name)) {
        emit log(QStringLiteral("web module %1: the container opened no page").arg(name));
        return -1;
    }

    // THE VIEW'S OWN REPORT. `logos-view: ready <name> count=0` is the module's
    // QML saying it took its backend replica over the MessagePort -- the fact a
    // screenshot of a canvas cannot establish.
    if (!waitForPageLine(QStringLiteral("logos-view: ready %1 count=").arg(name), 90000)) {
        emit log(QStringLiteral("web module %1: the view never reported a backend").arg(name));
        return -1;
    }
    return timer.elapsed();
}

bool WebModuleRunner::run()
{
    auto* backend = MobileWebContainerBackend::instance();
    const QStringList modules = available();

    if (modules.isEmpty()) {
        emit log(m_webModulesDir.isEmpty()
                     ? QStringLiteral("web modules: this build ships none")
                     : QStringLiteral("web modules: none discovered under %1")
                           .arg(m_webModulesDir));
        return true;
    }
    emit log(QStringLiteral("web modules: %1 (from %2)")
                 .arg(modules.join(QStringLiteral(", ")), m_webModulesDir));
    emit log(MobileWebContainerBackend::appMemoryLine(QStringLiteral("before any web module")));

    // ── the first module's UI, cold ────────────────────────────────────────
    const QString first = modules.first();
    const qint64 coldMs = bringUp(first);
    if (coldMs < 0) return false;
    // AGAINST THE SPIKE'S BASELINE, which is the number slice 28 asks this to
    // be logged against: 2.6-3 s for a Qt-wasm QML runtime's first paint,
    // measured in a desktop browser.
    emit log(QStringLiteral("COLD START: %1's UI ready at %2 ms "
                            "(spike baseline 2600-3000 ms)")
                 .arg(first).arg(coldMs));

    // Laid out, not merely alive: the fixture reports where its button is in
    // window coordinates, and a view that never got geometry reports 0 0.
    const bool laidOut = waitForPageLine(
        QStringLiteral("logos-view: button-at ([1-9][0-9]*) ([1-9][0-9]*)"), 20000);
    emit log(laidOut
                 ? QStringLiteral("web module %1: its view is laid out inside the page").arg(first)
                 : QStringLiteral("web module %1: its view never reported a button position")
                       .arg(first));

    // ── a real key and a real finger, at a canvas ──────────────────────────
    //
    // Slice 28's first criterion, and the half a rendered view does not
    // establish. Both go in as DOM events at the page and both are reported by
    // the module's own QML; see kInputDriver for why they cannot be anything
    // else.
    const bool tapped = tapButton(first);
    // THE LIST BEFORE THE FIELD, and the order is load-bearing on a phone:
    // giving a text field focus raises the soft keyboard over the bottom of the
    // screen, and the list is under it. Measured on a Samsung SM-G990B -- a
    // swipe aimed at the list's own coordinates landed on the keyboard and the
    // list never moved.
    const bool scrolled = scrollViewList(first);
    const bool typed = typeIntoView(first);

    // WHAT A HOST CAN AND CANNOT DRIVE, and the difference is the engine's
    // rather than the container's.
    //
    // A `web` variant's view is a canvas, so the events above are dispatched IN
    // the page. WKWebView acts on them: the tap, the keys and the drag all
    // reach the scene and the module reports each (measured on an iPhone 16 Pro
    // simulator). Android's WebView does NOT -- Qt receives the synthetic
    // pointer event and calls preventDefault on it, but the scene never acts --
    // while a REAL touch drives the same build of the same module perfectly:
    //
    //     adb shell input tap <x> <y>        # the device coordinates the
    //     adb shell input text logos         # driver logs above
    //     adb shell input swipe <x> <y1> <x> <y2> 200
    //
    // So the three are asserted when anything landed and REPORTED when nothing
    // did: a run driven from outside satisfies them exactly as an in-page run
    // does, and a platform whose webview refuses synthetic input is not a
    // failing container.
    const int reacted = int(tapped) + int(typed) + int(scrolled);
    const bool inputDriven = reacted > 0;
    if (!inputDriven) {
        emit log(QStringLiteral(
            "INPUT: nothing this host dispatched into %1's page reached the scene. "
            "This platform's webview does not act on a synthetic DOM event; drive it "
            "from outside instead (`adb shell input tap|text|swipe`) at the device "
            "coordinates the `logos-drive: at` lines above report, and the module "
            "reports all three.").arg(first));
    }

    if (modules.size() < 2) {
        emit log(QStringLiteral("live-runtime budget: only one web module is installed, "
                                "so there is nothing to evict"));
        return laidOut && (!inputDriven || (tapped && typed && scrolled));
    }

    // ── the second module, and the budget ──────────────────────────────────
    const QString second = modules.at(1);
    QStringList evicted;
    const auto evictConnection =
        connect(backend, &MobileWebContainerBackend::uiEvicted, this,
                [&evicted](const QString& name) { evicted.append(name); });
    // The other answer, for a package that ships no headless document. Counted
    // separately so a run against an older variant says which happened.
    QStringList unloadable;
    const auto unloadConnection =
        connect(backend, &MobileWebContainerBackend::uiEvictionRequired, this,
                [&unloadable](const QString& name) { unloadable.append(name); });

    const qint64 held = basecamp::web::appResidentBytes();
    const qint64 secondMs = bringUp(second);
    disconnect(evictConnection);
    disconnect(unloadConnection);
    if (secondMs < 0) return false;
    emit log(QStringLiteral("COLD START: %1's UI ready at %2 ms").arg(second).arg(secondMs));

    if (!evicted.contains(first)) {
        emit log(unloadable.contains(first)
                     ? QStringLiteral("WRONG: %1's package ships no headless entry "
                                      "document, so it could only be unloaded")
                           .arg(first)
                     : QStringLiteral("WRONG: showing %1 did not put %2 over the "
                                      "live-runtime budget").arg(second, first));
        return false;
    }

    // THE CONTAINER ANSWERED IT ITSELF, which is what is new here. A module
    // whose package ships a headless entry document is not unloaded: its page
    // is swapped onto that document, so the view, the bridge and the channel
    // the core holds are all still the same objects and the core was never
    // told. The host's only job is to stop mounting a surface that is gone.
    //
    // The webview's own teardown is asynchronous on both platforms, and what is
    // reclaimed is reclaimed by the OS afterwards. Two seconds is what the
    // number is worth reading after.
    pump(2000);
    const qint64 after = basecamp::web::appResidentBytes();

    const bool released = backend->hasView(first) && !backend->hasUiPage(first);
    emit log(released
                 ? QStringLiteral("live-runtime budget: %1 gave up its UI page for %2 "
                                  "and stayed loaded").arg(first, second)
                 : QStringLiteral("WRONG: %1 still holds a UI page after being evicted")
                       .arg(first));
    emit log(QStringLiteral("live-runtime budget: %1 of %2 held by %3 live runtime(s)")
                 .arg(megabytes(backend->budget().projectedBytes()),
                      megabytes(backend->budget().budgetBytes()),
                      QString::number(backend->budget().live().size())));
    // THE HOST'S OWN FOOTPRINT, AND WHAT IT DOES NOT INCLUDE. Both phones run a
    // webview's content in a process of their own -- WebKit's WebContent,
    // Chromium's sandboxed renderer -- and an embedder cannot weigh either: iOS
    // offers no API for another task's footprint and Android's renderer runs
    // under a different uid, so its /proc is not ours to read. So the page's
    // 185-240 MB is NOT in these numbers, and saying so beats printing a
    // difference of zero as though the runtime cost nothing.
    //
    // What a shell CONTROLS is how many pages are alive, which is the line
    // above; this one is what the app itself holds either side of an eviction.
    emit log(QStringLiteral("app footprint %1 -> %2 (the evicted page's own cost is in "
                            "the platform's WebContent/renderer process, which an "
                            "embedder cannot weigh)")
                 .arg(megabytes(held), megabytes(after)));

    // ── and the evicted module is still ANSWERING ──────────────────────────
    //
    // Slice 28's third criterion. Three facts, in the order they build on each
    // other: the core still has the module loaded; its headless page said its
    // Wasm host came up; and a frame sent into that page came back answered.
    const bool stillLoaded = m_core->loadedModules().contains(first);
    emit log(stillLoaded
                 ? QStringLiteral("web module %1: still loaded with its UI evicted").arg(first)
                 : QStringLiteral("WRONG: %1 left the core's loaded set").arg(first));

    const bool hostUp = waitForPageLine(
        QStringLiteral("\\[logos-web-host %1\\] serving with no UI").arg(first), 60000);
    emit log(hostUp
                 ? QStringLiteral("web module %1: its Wasm host reports itself serving with "
                                  "no UI, from the headless entry document").arg(first)
                 : QStringLiteral("WRONG: %1's headless page never reported a serving host")
                       .arg(first));

    QString answer;
    const bool answered = callIntoPage(first, &answer);
    emit log(answered
                 ? QStringLiteral("BACKGROUND CALL: %1 answered the container's own "
                                  "interface query with its UI evicted -- %2")
                       .arg(first, answer.left(160))
                 : QStringLiteral("WRONG: %1 did not answer a frame sent into its "
                                  "headless page").arg(first));

    return laidOut && (!inputDriven || (tapped && typed && scrolled))
           && released && stillLoaded && hostUp && answered;
}
