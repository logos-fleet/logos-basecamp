// THE WEB CONTAINER, END TO END, ON A PHONE: the app's shipped `web` modules
// loaded through the real core into real webviews, driven, and weighed.
//
// The desktop has the same check as a nix derivation (nix/web-container-test.nix
// over tests/web-container/main.cpp). This is what a device adds over it, and it
// is slice 28's whole subject:
//
//   * that a Qt-wasm QML runtime BOOTS in this platform's webview at all, under
//     Qt's separate-main-stack entry;
//   * what a Downloaded module's UI costs to cold-start HERE, against the
//     spike's 2.6-3 s desktop-browser baseline;
//   * that showing a second Downloaded module puts the first over the
//     live-runtime budget, and that the app's memory comes back when it gives
//     its UI page up.
//
// WHAT IT READS TO KNOW WHAT HAPPENED: the page's own console, forwarded by
// MobileWebContainerBackend::pageLog. A view draws into a canvas; from outside
// there is no DOM to query and no label to read. The fixture variant says what
// it did -- when its replica arrived, where its button is -- and those lines are
// emitted by the module's own QML reacting to its own backend, which is exactly
// the fact under test.
#pragma once

#include "ICoreRuntime.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class WebModuleRunner : public QObject
{
    Q_OBJECT
public:
    // `core` is the runtime the shell drives; `webModulesDir` is where this
    // platform put the app's shipped `web` packages, and is reported rather
    // than assumed so a build that shipped none says which directory was empty.
    WebModuleRunner(ICoreRuntime* core, QString webModulesDir, QObject* parent = nullptr);

    // The `web` modules the app ships AND the core discovered, in the order the
    // directory names them. Empty is a legitimate state: a build without the
    // qml-runtime-wasm pin ships no variant.
    QStringList available() const;

    // Bring the first one up, drive it, then bring the second one up and let
    // the budget evict the first. True when every step this app could take
    // succeeded; a set with no `web` module in it is reported and is NOT a
    // failure -- there is nothing to fail.
    bool run();

signals:
    void log(const QString& line);

private:
    // Load `name` through the core, show it, and wait for its view to report
    // itself. Milliseconds from the load call to the view's `ready` line, or -1.
    qint64 bringUp(const QString& name);
    // Pump the event loop until a page line matches `pattern`, or `timeoutMs`.
    bool waitForPageLine(const QString& pattern, int timeoutMs);
    // ...and the capture groups of the line that matched, or an empty list.
    QStringList capturePageLine(const QString& pattern, int timeoutMs);
    void pump(int ms);

    // DRIVE REAL INPUT AT `name`'s VIEW. A `web` variant draws into a canvas:
    // there is no DOM node to touch and no text node to read, so the only way
    // to put a key or a finger on what a user would touch is to dispatch the
    // event in the page. What happens next the MODULE reports, on its own
    // console, which is what these then wait for.
    bool tapButton(const QString& name);
    bool typeIntoView(const QString& name);
    bool scrollViewList(const QString& name);

    // ...and the one question that cannot be asked through the core: is this
    // module still answering? Sends a logos-protocol Call into its page and
    // waits for a Result carrying the same id.
    bool callIntoPage(const QString& name, QString* answer);

    ICoreRuntime* m_core = nullptr;
    QString m_webModulesDir;
    QStringList m_pageLines;
};
