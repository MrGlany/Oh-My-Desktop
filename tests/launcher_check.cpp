#include "launcherutil.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QJsonDocument>
#include <QJsonArray>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--scope-probe") {
        if (!LauncherUtil::startDetachedWindowedProgram("/usr/bin/python3", {QString::fromLocal8Bit(argv[2])})) return 1;
        QThread::sleep(60);
        return 0;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) return 1;
    const QString output=dir.path()+"/argv.json";
    QFile runner(dir.path()+"/systemd-run");
    if (!runner.open(QIODevice::WriteOnly)) return 2;
    runner.write("#!/usr/bin/python3\nimport os,sys,json\nopen(os.environ['OMD_LAUNCH_TEST_OUTPUT'],'w').write(json.dumps(sys.argv[1:]))\n");
    runner.close();
    runner.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    qputenv("PATH",dir.path().toUtf8());
    qputenv("DBUS_SESSION_BUS_ADDRESS","test-only");
    qputenv("OMD_LAUNCH_TEST_OUTPUT",output.toUtf8());
    const QStringList args{"file with spaces", "$HOME", "$(touch should-not-run)", "--option"};
    if (!LauncherUtil::startDetachedWindowedProgram("test-app",args)) return 3;
    for (int n=0;n<100 && !QFile::exists(output);++n) QThread::msleep(10);
    QFile file(output);if (!file.open(QIODevice::ReadOnly)) return 4;
    auto result=QJsonDocument::fromJson(file.readAll()).array();
    QStringList actual;for (const auto &v:result) actual.append(v.toString());
    QStringList expected{"--user","--scope","--collect","--quiet","--slice=app.slice","--expand-environment=no","--","test-app"};
    expected.append(args);
    if (actual!=expected) return 5;
    file.close();QFile::remove(output);
    const QString command="printf '%s' '$HOME; $(literal)'";
    if (!LauncherUtil::startDetachedWindowedCommand(command)) return 6;
    for (int n=0;n<100 && !QFile::exists(output);++n) QThread::msleep(10);
    if (!file.open(QIODevice::ReadOnly)) return 7;
    result=QJsonDocument::fromJson(file.readAll()).array();actual.clear();
    for (const auto &v:result) actual.append(v.toString());
    if (actual.mid(7)!=QStringList{"/bin/sh","-c",command}) return 8;
    return 0;
}
