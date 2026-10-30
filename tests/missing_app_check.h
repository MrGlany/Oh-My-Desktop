#pragma once
#include "desktopapplaunch.h"
#include <QMessageBox>
#include <QPushButton>
#include <functional>

static int missingAppCheck(){
    const QString root=qEnvironmentVariable("XDG_CONFIG_HOME")+"/missing-app-fixture";
    if(!root.contains("properties-check-missing-app"))return 94;
    QDir().mkpath(root+"/Desktop");QWidget owner;
    const QString absent=root+"/removed-app";
    const QByteArray bytes=("[Desktop Entry]\nType=Application\nName=Removed fixture\nExec=\""+absent+"\"\n").toUtf8();
    auto write=[&](const QString &path,const QByteArray &data){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size();};
    auto interact=[&](const QString &path,const std::function<void(QMessageBox*)> &action,bool duplicate=false){
        bool seen=false,valid=false;QTimer poll;poll.setInterval(20);
        QObject::connect(&poll,&QTimer::timeout,[&]{
            auto *box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());if(!box)return;
            if(box->objectName()=="MissingApplicationShortcut"&&!seen){
                seen=true;valid=box->palette().window().color()==QColor(32,32,39)&&box->textFormat()==Qt::PlainText&&box->defaultButton()==box->findChild<QPushButton*>("KeepShortcut")&&box->escapeButton()==box->findChild<QPushButton*>("KeepShortcut");
                action(box);
            }else if(box->objectName()!="MissingApplicationShortcut"){fprintf(stderr,"Follow-up: %s\n",qPrintable(box->text()));box->accept();}
        });
        poll.start();DesktopAppLauncher::open(path,&owner);if(duplicate)DesktopAppLauncher::open(path,&owner);
        for(int wait=0;wait<150 && !qApp->findChildren<QObject*>("DesktopAppLaunchJob").isEmpty();++wait)QTest::qWait(20);
        poll.stop();return seen&&valid&&qApp->findChildren<QObject*>("DesktopAppLaunchJob").isEmpty();
    };
    // Double-click dispatch must reach the same scene opening path as Enter
    // and the context menu, instead of bypassing validation in DesktopIcon.
    const QString dispatchPath=root+"/Desktop/Dispatch.desktop";if(!write(dispatchPath,bytes))return 139;
    {DesktopIcon icon(QFileInfo(dispatchPath),40,false);QSignalSpy requests(&icon,&DesktopIcon::openRequested);QTest::mouseDClick(&icon,Qt::LeftButton);if(requests.count()!=1)return 140;}
    QFile::remove(dispatchPath);
    auto keep=[](QMessageBox *box){box->findChild<QPushButton*>("KeepShortcut")->click();};
    auto trash=[](QMessageBox *box){box->findChild<QPushButton*>("TrashShortcut")->click();};
    QString path=root+"/Desktop/Keep.desktop";
    if(!write(path,bytes)||!interact(path,keep,true)||!QFileInfo::exists(path))return 95;
    path=root+"/Desktop/Trash.desktop";if(!write(path,bytes)||!interact(path,trash)||QFileInfo::exists(path)){fprintf(stderr,"Trash fixture remains=%d\n",QFileInfo::exists(path));return 96;}
    // Trashing a symlink must leave its target and unrelated files intact.
    const QString original=root+"/Original.desktop";if(!write(original,bytes))return 97;
    path=root+"/Desktop/Link.desktop";
    if(!QFile::link(original,path)||!interact(path,trash)||QFileInfo(path).isSymLink()||!QFileInfo::exists(original))return 98;
    path=root+"/Desktop/Edited.desktop";const QByteArray edited=bytes+"Comment=Edited while dialog open\n";
    if(!write(path,bytes)||!interact(path,[&](QMessageBox *box){write(path,edited);trash(box);}))return 99;
    QFile changed(path);if(!changed.open(QIODevice::ReadOnly)||changed.readAll()!=edited)return 134;changed.close();
    path=root+"/Desktop/Replaced.desktop";
    if(!write(path,bytes)||!interact(path,[&](QMessageBox *box){QFile::rename(path,path+".original");write(path,bytes);trash(box);})||!QFileInfo::exists(path)||!QFileInfo::exists(path+".original"))return 135;
    path=root+"/Desktop/Reinstalled.desktop";
    if(!write(path,bytes)||!interact(path,[&](QMessageBox *box){write(absent,"#!/bin/sh\nexit 0\n");QFile::setPermissions(absent,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner);trash(box);})||!QFileInfo::exists(path))return 136;
    QFile target(original);if(!target.open(QIODevice::ReadOnly)||target.readAll()!=bytes)return 137;
    const QDir files(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/Trash/files");
    if(files.entryList(QDir::Files|QDir::System|QDir::NoDotAndDotDot).size()!=2)return 138;
    puts("PASS missing-app dialog: default Keep, trash only launcher/link, original preserved, changed/replaced files preserved, reinstalled app rechecked, duplicate-open guard");return 0;
}
