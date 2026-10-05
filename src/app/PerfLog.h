#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QString>
#include <cstdio>

// Optional timing log for measuring start-up and file-open speed.
//   TRIMFAST_PERF=1     prints "perf: <event> +<ms> ms (rss <MB>)" lines to stderr
//   TRIMFAST_PERF=quit  the same, and the application quits once the file (or, without a
//                       file, the window) is ready, so that it can be scripted.
// Disabled (and free) unless the variable is set.
namespace PerfLog {

inline QElapsedTimer &clock()
{
    static QElapsedTimer t;
    return t;
}

inline bool enabled()
{
    static const bool on = !qEnvironmentVariableIsEmpty("TRIMFAST_PERF");
    return on;
}

inline bool quitWhenReady()
{
    static const bool quit = qgetenv("TRIMFAST_PERF") == "quit";
    return quit;
}

inline void start()
{
    clock().start();
}

// Resident memory in MiB (Linux: /proc; -1 where it is not available).
inline double residentMiB()
{
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/self/statm"));
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> fields = f.readAll().split(' ');
        if (fields.size() > 1)
            return fields.at(1).toDouble() * 4096.0 / (1024.0 * 1024.0);
    }
#endif
    return -1.0;
}

inline void mark(const char *event)
{
    if (!enabled() || !clock().isValid())
        return;
    const double rss = residentMiB();
    if (rss >= 0)
        std::fprintf(stderr, "perf: %-22s +%6lld ms   rss %7.1f MiB\n", event,
                     static_cast<long long>(clock().elapsed()), rss);
    else
        std::fprintf(stderr, "perf: %-22s +%6lld ms\n", event, static_cast<long long>(clock().elapsed()));
}

} // namespace PerfLog
