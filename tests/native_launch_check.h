#pragma once
#include "launcherutil.h"
#include <QMessageBox>
#include <QJsonArray>

static int nativeLaunchCheck(){
    const QString root=qEnvironmentVariable("OMD_DESKTOP_DIR");if(!root.contains("properties-check-native-launch"))return 169;
    QDir().mkpath(root+"/bundle with spaces; $(literal)");
    const QString binary=root+"/bundle with spaces; $(literal)/AniHot App",link=root+"/Shortcut",output=root+"/launch.json";
    if(!QFile::copy("/usr/bin/true",binary)||!QFile::link(binary,link))return 170;
    QFile bytes(binary);if(!bytes.open(QIODevice::ReadOnly))return 171;const QByteArray before=bytes.readAll();bytes.close();
    QFile::setPermissions(binary,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    if(LauncherUtil::nativeExecutablePath(QFileInfo(link))!=binary)return 172;
    const QString runner=root+"/systemd-run";
    QFile fake(runner);if(!fake.open(QIODevice::WriteOnly))return 173;
    fake.write("#!/usr/bin/python3\nimport os,sys,json\nopen(os.environ['OMD_NATIVE_LAUNCH_OUTPUT'],'w').write(json.dumps({'argv':sys.argv[1:],'cwd':os.getcwd()}))\n");fake.close();QFile::setPermissions(runner,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);
    const auto oldPath=qgetenv("PATH"),oldBus=qgetenv("DBUS_SESSION_BUS_ADDRESS");
    qputenv("PATH",QFile::encodeName(root));qputenv("DBUS_SESSION_BUS_ADDRESS","test-only");qputenv("OMD_NATIVE_LAUNCH_OUTPUT",QFile::encodeName(output));
    DesktopScene scene(QGuiApplication::primaryScreen());
    QMetaObject::invokeMethod(&scene,"onOpenRequested",Qt::DirectConnection,Q_ARG(QFileInfo,QFileInfo(link)));
    for(int wait=0;wait<100&&!QFile::exists(output);++wait)QTest::qWait(20);
    QFile result(output);if(!result.open(QIODevice::ReadOnly))return 174;
    const auto json=QJsonDocument::fromJson(result.readAll()).object();result.close();QFile::remove(output);
    QStringList argv;for(const auto &item:json.value("argv").toArray())argv<<item.toString();
    if(argv!=QStringList{"--user","--scope","--collect","--quiet","--slice=app.slice","--expand-environment=no","--",binary}||json.value("cwd").toString()!=QFileInfo(binary).absolutePath())return 175;
    // Exec bits on documents/scripts/libraries do not make them native apps.
    for(const auto &name:{"notes.txt","script.sh","library.so"}){
        const QString path=root+"/"+name;QFile f(path);if(!f.open(QIODevice::WriteOnly))return 176;
        f.write(name==QString("library.so")?before:QByteArray("#!/bin/sh\nexit 0\n"));f.close();QFile::setPermissions(path,QFileDevice::ReadOwner|QFileDevice::ExeOwner);
        if(!LauncherUtil::nativeExecutablePath(QFileInfo(path)).isEmpty())return 177;
    }
    QFile::setPermissions(binary,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    bool warning=false;QTimer::singleShot(100,[&]{auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(box){warning=box->text().contains("нет права");box->accept();}});
    QMetaObject::invokeMethod(&scene,"onOpenRequested",Qt::DirectConnection,Q_ARG(QFileInfo,QFileInfo(link)));
    if(!warning||QFile::exists(output)||QFileInfo(binary).isExecutable())return 178;
    qputenv("PATH",oldPath);qputenv("DBUS_SESSION_BUS_ADDRESS",oldBus);
    QFile after(binary);if(!after.open(QIODevice::ReadOnly)||after.readAll()!=before||!QFileInfo(link).isSymLink())return 179;
    puts("PASS desktop symlink -> native ELF directly in independent scope, target working directory, literal spaces/metacharacters; documents/scripts/libraries excluded, missing permissions preserved");return 0;
}
