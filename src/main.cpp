#include "desktopwindow.h"
#include "pagecontrol.h"
#include "dolphinintegration.h"
#ifdef OMD_BUILD_SMOKE
#include "smoketest.h"
#endif

#include <QApplication>
#include <QFile>
#include <QScreen>
#include <QHash>
#include <QIcon>
#include <QTranslator>
#include <QLibraryInfo>
#include <QSocketNotifier>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

static int shutdownWriteFd = -1;
static void requestShutdown(int) {
    const char byte = 1;
    if (shutdownWriteFd >= 0) { const auto ignored = write(shutdownWriteFd, &byte, 1); (void)ignored; }
}

int main(int argc, char *argv[])
{
    if(argc==2&&QByteArray(argv[1])=="--cleanup-dolphin-menu"){
        QCoreApplication cleanup(argc,argv);
        return DolphinIntegration::cleanupStopped()?0:1;
    }
    int shutdownPipe[2];
    if (pipe2(shutdownPipe, O_NONBLOCK | O_CLOEXEC) != 0) return 1;
    shutdownWriteFd = shutdownPipe[1];
    struct sigaction action{};
    action.sa_handler = requestShutdown;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    QApplication app(argc, argv);
    QSocketNotifier notifier(shutdownPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
        char byte;
        if (read(shutdownPipe[0], &byte, 1) == 1) app.quit();
    });
    QTranslator russianQt;
    if (russianQt.load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&russianQt);
    app.setApplicationName("Oh My Desktop");
    app.setOrganizationName("Oh My Desktop");
    app.setQuitOnLastWindowClosed(false);
    PageControl control;
    if(!control.listen())return 1;
    if (QIcon::fromTheme("folder").isNull()) QIcon::setThemeName("breeze");

    // Load retro stylesheet
    QFile qssFile(":/styles/styles/retro.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    }

    // Create one desktop window per screen
    QHash<QScreen *, DesktopWindow *> windows;
    for (QScreen *screen : app.screens()) {
        if (!windows.contains(screen)) windows.insert(screen, new DesktopWindow(screen));
    }

    // Handle screens added at runtime
    QObject::connect(&app, &QApplication::screenAdded, [&](QScreen *screen) {
        if (!windows.contains(screen)) windows.insert(screen, new DesktopWindow(screen));
    });

    // Handle screen removal - hot-unplug cleanup
    QObject::connect(&app, &QApplication::screenRemoved, [&](QScreen *screen) {
        if (auto *w = windows.take(screen)) {
            w->close();
            w->deleteLater();
        }
    });

    DolphinIntegration dolphin;
    if(!dolphin.activate())qWarning("Dolphin menu could not be registered; desktop files are unaffected");
    QObject::connect(&app,&QCoreApplication::aboutToQuit,[&]{dolphin.deactivate();});

#ifdef OMD_BUILD_SMOKE
    scheduleSmokeTest(windows);
#endif
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        qDeleteAll(windows);
        windows.clear();
    });
    const int result = app.exec();
    shutdownWriteFd = -1;
    close(shutdownPipe[0]);
    close(shutdownPipe[1]);
    return result;
}
