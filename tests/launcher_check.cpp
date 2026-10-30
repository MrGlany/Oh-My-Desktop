#include "launcherutil.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QThread>
#include <QJsonDocument>
#include <QJsonArray>
#include <cstdio>
#include <QUrl>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if(argc==3 && QString::fromLocal8Bit(argv[1])=="--native-path-check"){
        const QString path=LauncherUtil::nativeExecutablePath(QFileInfo(QString::fromLocal8Bit(argv[2])));
        if(path.isEmpty())return 9;
        puts("PASS selected file is a native Linux application; no execution performed");return 0;
    }
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
    file.close();QFile::remove(output);
    const QString folder=dir.path()+"/Source folder",source=folder+"/файл # $(literal).txt",link=dir.path()+"/shortcut";
    if(!QDir().mkpath(folder))return 10;
    QFile original(source);if(!original.open(QIODevice::WriteOnly))return 11;original.write("preserve me");original.close();
    if(!QFile::link(source,link))return 12;
    for(const auto &path:{link,folder}){
        if(!LauncherUtil::revealFile(QFileInfo(path)))return 13;
        for(int n=0;n<100&&!QFile::exists(output);++n)QThread::msleep(10);
        if(!file.open(QIODevice::ReadOnly))return 14;
        const auto args=QJsonDocument::fromJson(file.readAll()).array();file.close();QFile::remove(output);
        if(args.size()!=10||args[7].toString()!="dolphin"||args[8].toString()!="--select"||QUrl(args[9].toString()).toLocalFile()!=(path==link?source:folder))return 15;
    }
    if(!QFile::link(dir.path()+"/absent",dir.path()+"/offline")||LauncherUtil::revealFile(QFileInfo(dir.path()+"/offline"))||QFile::exists(output))return 16;
    if(!QFileInfo(link).isSymLink()||!original.open(QIODevice::ReadOnly)||original.readAll()!="preserve me")return 17;
    puts("PASS independent launch and Dolphin reveal: link target, folder selection, literal paths, missing target preserved");
    return 0;
}
