#include "webview/AndroidWebPage.h"

#include "webview/MobileWebBridge.h"

#include <jni.h>

#include <QCoreApplication>
#include <QFuture>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
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
    const auto usable = [](const QString& dir) {
        return !dir.isEmpty()
               && QFileInfo(dir).isDir()
               && QFileInfo(QDir(dir).filePath(QStringLiteral("logos_qml_runtime.js"))).isFile();
    };

    const QString fromEnv = qEnvironmentVariable("LOGOS_QML_RUNTIME_DIR");
    if (!fromEnv.isEmpty()) {
        if (usable(fromEnv)) return QDir(fromEnv).absolutePath();
        qWarning() << "LOGOS_QML_RUNTIME_DIR points at" << fromEnv
                   << "which holds no logos_qml_runtime.js; ignoring it";
    }

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString candidate = QDir(appData).filePath(QStringLiteral("logos-runtime"));
    return usable(candidate) ? QDir(candidate).absolutePath() : QString();
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
        QNativeInterface::QAndroidApplication::runOnAndroidMainThread(
            [page, handle, url]() {
                QJniObject activity = QNativeInterface::QAndroidApplication::context();
                *page = QJniObject::callStaticObjectMethod(
                    kPageClass, "create",
                    "(Landroid/app/Activity;JLjava/lang/String;)Lco/logos/webview/LogosWebPage;",
                    activity.object(), handle, QJniObject::fromString(url).object<jstring>());
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
        return platform;
    };
}

} // namespace basecamp::web
