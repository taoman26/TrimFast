#include "app/MainWindow.h"
#include "app/PerfLog.h"
#include "app/TrimFastApplication.h"

#include <QFileInfo>
#include <QTimer>
#include <cstdio>
#include <cstring>

#ifndef TRIMFAST_VERSION
#define TRIMFAST_VERSION "0.0.0"
#endif


namespace {

void printUsage(std::FILE *out)
{
    std::fputs("TrimFast " TRIMFAST_VERSION " - lossless video trimmer (FFmpeg stream copy)\n"
               "\n"
               "Usage: trimfast [file]\n"
               "       trimfast --help | --version\n"
               "\n"
               "Open a video, press I at the start and O at the end of the part to keep, then Ctrl+E.\n"
               "Needs the ffmpeg and ffprobe programs. Environment: TRIMFAST_AUDIO=0|1, TRIMFAST_PERF=1|quit.\n"
               "\n"
               "Home page and bug reports: " TRIMFAST_HOMEPAGE "\n",
               out);
}

} // namespace

int main(int argc, char *argv[])
{
    // Answered before any window system is touched: usable from scripts and package checks.
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-v")) {
            std::puts("TrimFast " TRIMFAST_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            printUsage(stdout);
            return 0;
        }
    }

    PerfLog::start();
    TrimFastApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TrimFast"));
    QApplication::setApplicationVersion(QStringLiteral(TRIMFAST_VERSION));
    // Ties the window to its .desktop file (icon and grouping under Wayland).
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.taoman26.TrimFast"));

    // The file to open (Qt has already removed its own options such as -platform).
    // "--" ends the options, so a file whose name starts with "-" can still be given.
    QString file;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--") && i + 1 < args.size()) {
            file = args.at(i + 1);
            break;
        }
        if (a.startsWith(QLatin1Char('-')) && !QFileInfo::exists(a)) {
            std::fprintf(stderr, "trimfast: unknown option '%s'\n\n", qPrintable(a));
            printUsage(stderr);
            return 2;
        }
        file = a;
        break;
    }

    MainWindow window;
    PerfLog::mark("window constructed");
    window.show();
    // First turn of the event loop: the window has been laid out and shown.
    QTimer::singleShot(0, [hasFile = !file.isEmpty()] {
        PerfLog::mark("window shown");
        if (!hasFile && PerfLog::quitWhenReady())
            QCoreApplication::quit();
    });

    // Files can also be handed over by the system (Tracker's "Open With", `open file`): a request
    // made while the window already exists, or one that arrived during start-up.
    QObject::connect(&app, &TrimFastApplication::fileOpenRequested, &window, &MainWindow::openPath);
    if (!file.isEmpty())
        window.openPath(file);
    else if (const QString pending = app.takePendingFile(); !pending.isEmpty())
        window.openPath(pending);
    return app.exec();
}
