// Brings the app's ONE Bundled Bare module up inside the running core and
// calls it.
//
// "Bundled" is the whole of what makes this different from installing a
// module: the image ships inside the app, in the only place its platform will
// dlopen from, and nothing about it is written at runtime.
//
//   iOS      <App>.app/Frameworks/bare_counter_bare.framework/bare_counter_bare,
//            copied in by Xcode with Code Sign On Copy, so it carries the app's
//            own signature. dyld will not load an unsigned image on a device,
//            which rules out staging a copy in the sandbox.
//   Android  <nativeLibraryDir>/libbare_counter_bare.so, unpacked from the APK.
//            Since API 29 dlopen() of anything under the app's writable data
//            directory is a W^X violation, so the same rule applies for a
//            different reason.
//
// Both directories are flat and read-only, so there is no manifest beside the
// image: it is compiled into this host (BundledManifest.h, generated from
// mobile/liblogos-smoke/modules/bare_counter/manifest.json) and handed to
// logos_core_add_bare_module together with the path.
#pragma once

#include <QObject>
#include <QString>

class BundledModuleRunner : public QObject
{
    Q_OBJECT
public:
    explicit BundledModuleRunner(QObject* parent = nullptr);
    ~BundledModuleRunner() override;

    // Registers the bundled image with the core, loads it and calls
    // add(1, 2). Returns true only if the call came back with 3 — the point
    // of a stateless method is that there is exactly one right answer.
    //
    // Must run after logos_core_start().
    bool run();

    // Where the image is, resolved from the running process rather than
    // guessed: dladdr() on a logos-protocol symbol names the image that
    // symbol came out of, and on both platforms the Bare module sits in a
    // known place relative to it. Public so the failure can be reported with
    // the path that was tried.
    static QString bundledImagePath();

signals:
    void log(const QString& line);

private:
    QString m_moduleName;
};
