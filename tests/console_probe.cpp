// Helper of tst_logging: prints in every way the application and the programs
// it starts can print, after setting up the console like main() does.
#include "logger.h"

#include <QCoreApplication>
#include <QProcess>

#include <cstdio>

int main(int argc, char** argv)
{
    gds::installQtMessageRouting();
    QCoreApplication app(argc, argv);
    if (argc > 1)
        gds::Logger::instance().setDirectory(QString::fromLocal8Bit(argv[1]));
    gds::isolateConsole();

    std::fprintf(stderr, "library-raw-stderr\n"); // e.g. fontconfig
    std::fflush(stderr);
    std::printf("library-raw-stdout\n");
    std::fflush(stdout);
    qDebug("library-debug");
    qWarning("library-warning");
    // a started program (browser, file manager) inherits stdout and stderr
    QProcess::execute(QStringLiteral("sh"),
        {QStringLiteral("-c"), QStringLiteral("echo child-stdout; echo child-stderr >&2")});
    qCritical("own-critical");
    return 0;
}
