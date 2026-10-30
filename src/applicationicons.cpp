#include "applicationicons.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <memory>
#include <sys/stat.h>

namespace {
QString key(const QString &path){return QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex();}
QString identity(const QString &path){struct stat s{};if(::lstat(QFile::encodeName(path).constData(),&s)!=0)return {};return QString::number(s.st_dev)+":"+QString::number(s.st_ino);}
QString settingsPath(){return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)+"/icon-overrides.ini";}
QString stamp(const QFileInfo &file){QFileInfo target(file.isSymLink()?file.symLinkTarget():file.absoluteFilePath());return target.absoluteFilePath()+"|"+QString::number(target.size())+"|"+QString::number(target.lastModified().toMSecsSinceEpoch());}
QIcon imageIcon(const QString &path){
    QFileInfo f(path);if(!f.isFile()||f.size()>16*1024*1024)return {};
    QImageReader reader(path);if(!reader.canRead())return {};
    const QSize size=reader.size();if(size.width()>8192||size.height()>8192)return {};
    return QIcon(path);
}
}
ApplicationIcons *ApplicationIcons::instance(){static auto *object=new ApplicationIcons(qApp);return object;}
QString ApplicationIcons::overridePath(const QFileInfo &file)const{
    QSettings settings(settingsPath(),QSettings::IniFormat);settings.beginGroup(key(file.absoluteFilePath()));
    const auto id=identity(file.absoluteFilePath());
    return !id.isEmpty()&&settings.value("identity").toString()==id?settings.value("image").toString():QString();
}
bool ApplicationIcons::setOverride(const QFileInfo &file,const QString &image){
    if(!image.isEmpty()&&imageIcon(image).isNull())return false;
    const QString id=identity(file.absoluteFilePath());if(id.isEmpty())return false;
    QSettings settings(settingsPath(),QSettings::IniFormat);settings.beginGroup(key(file.absoluteFilePath()));settings.remove("");
    if(!image.isEmpty()){settings.setValue("image",QFileInfo(image).absoluteFilePath());settings.setValue("identity",id);}
    settings.endGroup();settings.sync();if(settings.status()!=QSettings::NoError)return false;
    emit changed(file.absoluteFilePath());return true;
}
void ApplicationIcons::moveOverride(const QString &oldPath,const QString &newPath){
    QSettings settings(settingsPath(),QSettings::IniFormat);
    settings.beginGroup(key(oldPath));const auto image=settings.value("image").toString(),id=settings.value("identity").toString();settings.endGroup();
    if(!image.isEmpty()&&id==identity(newPath)){settings.beginGroup(key(newPath));settings.setValue("image",image);settings.setValue("identity",id);settings.endGroup();settings.remove(key(oldPath));settings.sync();}
}
QIcon ApplicationIcons::icon(const QFileInfo &file,bool ignoreOverride){
    const QString custom=ignoreOverride?QString():overridePath(file);if(!custom.isEmpty()){auto icon=imageIcon(custom);if(!icon.isNull())return icon;}
    if(!file.exists()||!file.isFile()||!(file.isExecutable()||file.suffix().compare("desktop",Qt::CaseInsensitive)==0||file.suffix().compare("exe",Qt::CaseInsensitive)==0||file.suffix().compare("appimage",Qt::CaseInsensitive)==0))return {};
    const QString path=file.absoluteFilePath(),current=stamp(file);
    const auto found=m_cache.constFind(path);
    if(found!=m_cache.cend()&&found->stamp==current&&QDateTime::currentMSecsSinceEpoch()-found->checked<60000)return found->icon;
    if(!m_pending.contains(path)){m_pending.insert(path);m_queue.enqueue(path);QTimer::singleShot(0,this,&ApplicationIcons::startNext);}
    return found!=m_cache.cend()&&found->stamp==current?found->icon:QIcon();
}
void ApplicationIcons::startNext(){
    while(m_running<2&&!m_queue.isEmpty()){
        const QString path=m_queue.dequeue(),before=stamp(QFileInfo(path));++m_running;
        auto *process=new QProcess(this);process->setReadChannel(QProcess::StandardOutput);
        auto handled=std::make_shared<bool>(false);
        const auto finish=[this,process,path,before,handled](bool ok){
            if(*handled)return;*handled=true;
            QIcon icon;const auto json=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
            if(ok&&process->exitCode()==0){
                if(json.contains("path"))icon=imageIcon(json.value("path").toString());
                else if(json.contains("theme"))icon=QIcon::fromTheme(json.value("theme").toString());
            }
            m_cache[path]={before,icon,QDateTime::currentMSecsSinceEpoch()};m_pending.remove(path);--m_running;
            process->deleteLater();emit changed(path);startNext();
        };
        connect(process,&QProcess::finished,this,[finish](int,QProcess::ExitStatus status){finish(status==QProcess::NormalExit);});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
        QTimer::singleShot(10000,process,[process]{if(process->state()!=QProcess::NotRunning)process->kill();});
        const QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/application-icons";
        process->start("/usr/bin/python3",{"-I",QCoreApplication::applicationDirPath()+"/application_icon.py",path,cache});
    }
}
