// One console line, on whichever phone the host is running on.
//
// This exists because the two platforms disagree about what a console IS.
// iOS gives a process's stderr to the debugger, to `simctl launch --console`
// and to devicectl, so an fprintf is the console. Android has no such thing:
// logcat reads the Android log, and a Qt app's stderr is discarded outright.
// Measured on an SM-G990B: the Shell installed, launched, came up and ran its
// whole acceptance pass, and `adb logcat` showed not one line of it -- the run
// looked like a hang.
#pragma once

#include <QtGlobal>
#include <QString>

#if defined(Q_OS_ANDROID)
#  include <android/log.h>
#else
#  include <cstdio>
#endif

namespace basecamp::mobile {

// The Android log tag every Logos line in the process carries. The SAME one
// BundledSetRunner installs its spdlog sink under, so `adb logcat -s
// logos-smoke` shows the core's own channels and the host's narration
// interleaved rather than in two places.
constexpr const char* kAndroidLogTag = "logos-smoke";

// `tag` is the host's own name for its lines -- "smoke" or "shell" -- and is
// what the runners grep for, so it is part of the MESSAGE on both platforms
// rather than a platform log tag.
inline void consoleLine(const char* tag, const QString& line)
{
#if defined(Q_OS_ANDROID)
    __android_log_print(ANDROID_LOG_INFO, kAndroidLogTag, "[%s] %s", tag,
                        qUtf8Printable(line));
#else
    std::fprintf(stderr, "[%s] %s\n", tag, qUtf8Printable(line));
    std::fflush(stderr);
#endif
}

} // namespace basecamp::mobile
