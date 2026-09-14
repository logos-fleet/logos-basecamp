#include "webview/AndroidWebPage.h"

#include "webview/MobileWebBridge.h"
#include "webview/MobileWebContainerBackend.h"

#include <jni.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QDebug>
#include <QDir>
#include <QJniEnvironment>
#include <QJniObject>
#include <QStandardPaths>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace basecamp::web {

namespace {

constexpr const char* kPageClass = "co/logos/webview/LogosWebPage";
constexpr const char* kReplyClass = "co/logos/webview/LogosWebPage$Reply";

using ServeFn = std::function<void(const QByteArray&, const QUrl&, const QByteArray&,
                                   MobileWebBridge::Respond)>;

// WHAT A `handle` NAMES. The Java side carries a jlong across the JNI boundary
// and hands it back on every request, so the native side needs a table rather
// than a pointer: a page that was destroyed while a blocked request was in
// flight would otherwise be dereferenced on the way back.
struct Registration {
    ServeFn serve;
    std::function<void()> onDied;
};

std::mutex& registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<jlong, std::shared_ptr<Registration>>& registry()
{
    static std::unordered_map<jlong, std::shared_ptr<Registration>> map;
    return map;
}

std::shared_ptr<Registration> lookup(jlong handle)
{
    std::lock_guard<std::mutex> guard(registryMutex());
    auto it = registry().find(handle);
    return it == registry().end() ? nullptr : it->second;
}

// Answer one request, BLOCKING until the bridge responds.
//
// Legal, and it is what makes the long poll free: Android calls
// shouldInterceptRequest on a background thread and documents that it may
// block. A poll that finds nothing is parked by the bridge and answered by the
// next send() or by the container's expiry timer, and this thread simply waits.
void JNICALL nativeServe(JNIEnv* env, jclass, jlong handle, jstring method,
                         jstring url, jobject out)
{
    auto registration = lookup(handle);
    if (!registration || !registration->serve) return;

    const QString methodText = QJniObject(method).toString();
    const QString urlText = QJniObject(url).toString();

    std::mutex mutex;
    std::condition_variable ready;
    bool answered = false;
    BridgeReply reply;

    registration->serve(methodText.toUtf8(), QUrl(urlText), QByteArray(),
                        [&](const BridgeReply& answer) {
                            std::lock_guard<std::mutex> guard(mutex);
                            reply = answer;
                            answered = true;
                            ready.notify_one();
                        });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [&answered] { return answered; });
    }

    jclass replyClass = env->GetObjectClass(out);
    env->SetIntField(out, env->GetFieldID(replyClass, "status", "I"), reply.status);
    env->SetObjectField(out, env->GetFieldID(replyClass, "mimeType", "Ljava/lang/String;"),
                        env->NewStringUTF(reply.mimeType.constData()));
    if (reply.isFile()) {
        env->SetObjectField(out, env->GetFieldID(replyClass, "filePath", "Ljava/lang/String;"),
                            env->NewStringUTF(reply.filePath.toUtf8().constData()));
    } else {
        jbyteArray bytes = env->NewByteArray(jsize(reply.body.size()));
        env->SetByteArrayRegion(bytes, 0, jsize(reply.body.size()),
                                reinterpret_cast<const jbyte*>(reply.body.constData()));
        env->SetObjectField(out, env->GetFieldID(replyClass, "body", "[B"), bytes);
    }
}

void JNICALL nativeDied(JNIEnv*, jclass, jlong handle)
{
    auto registration = lookup(handle);
    if (registration && registration->onDied) registration->onDied();
}

// Registered once. A second registration of the same class replaces the first,
// which is harmless but pointless.
bool registerNatives()
{
    static bool done = false;
    if (done) return true;
    // Built at run time because it names the NESTED Reply class, whose binary
    // name carries the `$` -- a detail a hand-written literal gets wrong once
    // and then fails at registration with "no such method".
    const std::string serveSignature =
        "(JLjava/lang/String;Ljava/lang/String;L" + std::string(kReplyClass) + ";)V";
    JNINativeMethod concrete[] = {
        { const_cast<char*>("nativeServe"), const_cast<char*>(serveSignature.c_str()),
          reinterpret_cast<void*>(nativeServe) },
        { const_cast<char*>("nativeDied"), const_cast<char*>("(J)V"),
          reinterpret_cast<void*>(nativeDied) },
    };
    QJniEnvironment env;
    done = env.registerNativeMethods(kPageClass, concrete, 2);
    if (!done) qWarning() << "Web container: could not register" << kPageClass << "natives";
    return done;
}

jlong nextHandle()
{
    static jlong handle = 0;
    return ++handle;
}

} // namespace

QString androidQmlRuntimeDir()
{
    // Where the app unpacks it from its assets. The rest of the question --
    // the override, what counts as a runtime -- is the same on both phones.
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return bundledQmlRuntimeDir(QDir(appData).filePath(QStringLiteral("logos-runtime")));
}

QString androidWebModulesDir()
{
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = QDir(appData).filePath(QStringLiteral("web-modules"));
    return QFileInfo(dir).isDir() ? QDir(dir).absolutePath() : QString();
}

// ── unpacking the `web` half of the APK ────────────────────────────────────
//
// AN APK'S ASSETS ARE NOT FILES. Qt reaches them through `assets:/`, which is a
// virtual file engine: it can open and read, but it cannot answer
// `canonicalFilePath()` -- and canonical-on-both-sides is exactly how
// LogosWebPaths::resolveUnder refuses a traversal out of the package. Serving a
// page straight out of `assets:/` would mean either a second, weaker traversal
// rule for one platform or no rule at all, and the whole point of
// LogosWebPaths is that all three containers answer "which file does this URL
// name" the same way.
//
// So the bytes are copied ONCE, on first launch, into the app's data directory
// where they are ordinary files. A stamp records what was unpacked; a new app
// build unpacks again, an unchanged one does not.
namespace {

bool copyAssetTree(const QString& from, const QString& to, qint64* bytes)
{
    QDir().mkpath(to);
    const QDir source(from);
    for (const QFileInfo& entry :
         source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyAssetTree(entry.filePath(), target, bytes)) return false;
            continue;
        }
        QFile::remove(target);
        if (!QFile::copy(entry.filePath(), target)) {
            qWarning() << "Web container: could not unpack" << entry.filePath();
            return false;
        }
        // A file copied out of the APK comes out read-only, and an unpack that
        // has to overwrite it next time would then fail.
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (bytes) *bytes += entry.size();
    }
    return true;
}

} // namespace

void unpackAndroidWebAssets(const QString& stamp)
{
    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString stampFile = QDir(appData).filePath(QStringLiteral(".web-assets-stamp"));

    QFile existing(stampFile);
    if (existing.open(QIODevice::ReadOnly)
        && QString::fromUtf8(existing.readAll()).trimmed() == stamp) {
        qInfo().noquote() << QStringLiteral("Web container: assets already unpacked (%1)")
                                 .arg(stamp);
        return;
    }
    existing.close();

    const QString root = QStringLiteral("assets:/logos-web");
    if (!QFileInfo(root).isDir()) {
        qWarning() << "Web container: this APK carries no assets/logos-web; it ships no "
                      "QML runtime and no `web` module";
        return;
    }

    QElapsedTimer timer;
    timer.start();
    qint64 bytes = 0;
    QDir().mkpath(appData);
    if (!copyAssetTree(root, appData, &bytes)) return;

    QFile record(stampFile);
    if (record.open(QIODevice::WriteOnly)) record.write(stamp.toUtf8());
    qInfo().noquote() << QStringLiteral("Web container: unpacked %1 MB of web assets in %2 ms")
                             .arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                             .arg(timer.elapsed());
}

PlatformPageFactory androidPlatformPageFactory()
{
    return [](const PlatformPageRequest& request) -> PlatformPage {
        if (!registerNatives()) return {};

        const jlong handle = nextHandle();
        auto registration = std::make_shared<Registration>();
        registration->serve = request.serve;
        registration->onDied = request.onDied;
        {
            std::lock_guard<std::mutex> guard(registryMutex());
            registry()[handle] = registration;
        }

        // ON THE ANDROID UI THREAD, and blocking until it has run: a WebView may
        // only be constructed there, and the container is waiting for a view.
        auto page = std::make_shared<QJniObject>();
        const QString url = request.entryUrl.toString();
        const QString host = request.entryUrl.host();
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
            [page, handle, url, host]() {
                QJniObject activity = QNativeInterface::QAndroidApplication::context();
                *page = QJniObject::callStaticObjectMethod(
                    kPageClass, "create",
                    "(Landroid/app/Activity;JLjava/lang/String;Ljava/lang/String;)"
                    "Lco/logos/webview/LogosWebPage;",
                    activity.object(), handle,
                    QJniObject::fromString(url).object<jstring>(),
                    QJniObject::fromString(host).object<jstring>());
            }).waitForFinished();

        if (!page->isValid()) {
            std::lock_guard<std::mutex> guard(registryMutex());
            registry().erase(handle);
            qWarning() << "Web module" << request.moduleName << "could not open a WebView";
            return {};
        }

        PlatformPage platform;
        platform.destroy = [page, handle] {
            // The registration goes FIRST, so a request still blocked in
            // nativeServe finds nothing and returns rather than calling into a
            // bridge the container is tearing down.
            {
                std::lock_guard<std::mutex> guard(registryMutex());
                registry().erase(handle);
            }
            QJniObject held = *page;
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
                [held]() { held.callMethod<void>("destroy"); }).waitForFinished();
            *page = QJniObject();
        };
        // THE JAVA OBJECT, not its WebView: a local reference to the view would
        // be dead by the time the shell looked at it, and the page holds a
        // strong one. `LogosWebPage.view()` is how the shell reaches the surface.
        platform.nativeHandle = [page]() -> void* {
            return page->isValid() ? static_cast<void*>(page->object()) : nullptr;
        };
        platform.isAlive = [page]() -> bool {
            return page->isValid() && page->callMethod<jboolean>("isAlive") != JNI_FALSE;
        };
        // ON THE ANDROID UI THREAD like every other touch of the view, and
        // blocking: the shell's next act is to say the module is visible, and a
        // z-order change still queued when that is read is a page the user does
        // not see.
        // ONE SCRIPT IN THE PAGE. On the Android UI thread like every other
        // touch of the view, but NOT blocking: a host driving input is not
        // waiting on a value -- what the script has to say arrives later, on the
        // page's own console, down the bridge.
        platform.evaluateJavaScript = [page](const QString& script) {
            if (!page->isValid()) return;
            QJniObject held = *page;
            QJniObject source = QJniObject::fromString(script);
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
                [held, source]() {
                    held.callMethod<void>("evaluateJavaScript", "(Ljava/lang/String;)V",
                                          source.object<jstring>());
                });
        };
        platform.setFrontmost = [page](bool front) {
            if (!page->isValid()) return;
            QJniObject held = *page;
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
                [held, front]() {
                    held.callMethod<void>("setFrontmost", "(Z)V", jboolean(front));
                }).waitForFinished();
        };
        // WHERE THE SHELL LEFT ROOM FOR IT (#110). A page added to the
        // Activity's content view fills it, so a page brought forward covers the
        // Shell's own navigation and the user cannot leave the app again. The
        // Shell docks a placeholder for a web app and publishes the rect its
        // workspace gave it.
        //
        // DEVICE PIXELS, converted here: an Android View's coordinates are
        // physical and Qt's are logical, which is the one thing the two phones
        // disagree about (iOS points ARE Qt's logical pixels). The container
        // stays in Qt's units so it does not have to know which phone it is on.
        // An empty rect gives the content view back.
        platform.setGeometry = [page](const QRect& windowRect) {
            if (!page->isValid()) return;
            const qreal scale = qApp ? qApp->devicePixelRatio() : 1.0;
            const QRect px(qRound(windowRect.x() * scale), qRound(windowRect.y() * scale),
                           qRound(windowRect.width() * scale),
                           qRound(windowRect.height() * scale));
            QJniObject held = *page;
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
                [held, px]() {
                    held.callMethod<void>("setGeometry", "(IIII)V", jint(px.x()), jint(px.y()),
                                          jint(px.width()), jint(px.height()));
                }).waitForFinished();
        };
        return platform;
    };
}

} // namespace basecamp::web
