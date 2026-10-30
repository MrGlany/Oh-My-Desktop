#include "dolphinintegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSettings>
#include <signal.h>
#include <errno.h>

static const QString managed="X-OhMyDesktop-Managed=true";
QString DolphinIntegration::menuPath(){
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/kio/servicemenus/oh-my-desktop-link.desktop";
}
bool DolphinIntegration::activate(){
    if(qEnvironmentVariableIsSet("OMD_DISABLE_DOLPHIN_MENU"))return true;
    QString helper=QCoreApplication::applicationDirPath()+"/send-to-desktop.py";
    if(!QFileInfo(helper).isFile())return false;
    const QString path=menuPath();
    QFile existing(path);
    if(QFileInfo(path).isSymLink())return false;
    if(existing.exists()){
        if(!existing.open(QIODevice::ReadOnly))return false;
        const QString content=QString::fromUtf8(existing.readAll());
        if(!content.split('\n').contains(managed))return false;
        existing.close();
    }
    helper.replace("\\","\\\\");helper.replace('"',"\\\"");helper.replace('`',"\\`");helper.replace('$',"\\$");helper.replace('%',"%%");
    const QString content="[Desktop Entry]\nType=Service\nName=Ярлык на рабочем столе\n"
        "MimeType=application/octet-stream;inode/directory;\nActions=DesktopLink;\nX-KDE-Protocols=file\nX-KDE-Priority=TopLevel\n"
        +managed+"\nX-OhMyDesktop-Pid="+QString::number(QCoreApplication::applicationPid())+"\n\n"
        "[Desktop Action DesktopLink]\nName=Создать ярлык на рабочем столе\nName[ru]=Создать ярлык на рабочем столе\n"
        "Icon=emblem-symbolic-link\nExec=/usr/bin/python3 -I \""+helper+"\" -- %F\n";
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    if(file.write(content.toUtf8())!=content.toUtf8().size()||!file.commit())return false;
    m_ownedContent=content;return true;
}
void DolphinIntegration::deactivate(){
    if(m_ownedContent.isEmpty())return;
    const QString path=menuPath();QFile file(path);
    if(!QFileInfo(path).isSymLink()&&file.open(QIODevice::ReadOnly)&&QString::fromUtf8(file.readAll())==m_ownedContent){file.close();QFile::remove(path);}
    m_ownedContent.clear();
}
bool DolphinIntegration::cleanupStopped(){
    const QString path=menuPath();if(!QFileInfo::exists(path))return true;
    if(QFileInfo(path).isSymLink())return false;
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
    const QString content=QString::fromUtf8(file.readAll());file.close();
    if(!content.split('\n').contains(managed))return false;
    QSettings settings(path,QSettings::IniFormat);
    const qint64 pid=settings.value("Desktop Entry/X-OhMyDesktop-Pid").toLongLong();
    // Never remove a menu owned by a still-running application instance.
    if(pid>0 && (::kill(pid,0)==0 || errno!=ESRCH))return false;
    return QFile::remove(path);
}
