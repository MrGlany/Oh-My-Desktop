#include "desktopapplaunch.h"
#include "desktoptextdialog.h"
#include "launcherutil.h"
#include "themeutil.h"
#include <QFontDatabase>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <sys/stat.h>
#include <memory>

namespace {
QSet<QString> pending;
class LaunchJob final : public QObject {
    QString path,name,command;
    QByteArray contents;
    QPointer<QWidget> owner;
    struct stat original{};
    bool snapshot(){
        if(::lstat(QFile::encodeName(path).constData(),&original)!=0||!QFileInfo(path).isFile())return false;
        QFile file(path);if(!file.open(QIODevice::ReadOnly))return false;
        contents=file.read(1024*1024+1);if(contents.size()>1024*1024)return false;
        bool main=false;
        for(const auto &raw:QString::fromUtf8(contents).split('\n')){
            const auto line=raw.trimmed();
            if(line.startsWith('[')){main=line=="[Desktop Entry]";continue;}
            if(!main||line.startsWith('#'))continue;
            if(line.startsWith("Name="))name=line.mid(5);
            if(line.startsWith("Exec="))command=line.mid(5);
        }
        if(name.isEmpty())name=QFileInfo(path).fileName();
        return true;
    }
    bool unchanged()const{
        struct stat now{};
        if(::lstat(QFile::encodeName(path).constData(),&now)!=0||now.st_dev!=original.st_dev||now.st_ino!=original.st_ino||now.st_mode!=original.st_mode||now.st_size!=original.st_size||now.st_mtim.tv_sec!=original.st_mtim.tv_sec||now.st_mtim.tv_nsec!=original.st_mtim.tv_nsec)return false;
        QFile file(path);return file.open(QIODevice::ReadOnly)&&file.read(1024*1024+1)==contents;
    }
    int show(QMessageBox &box){
        DesktopDialogs::DesktopKeyboardPause pause;
        box.setTextFormat(Qt::PlainText);
        const auto colors=desktopThemePalette();box.setPalette(colors);
        auto font=QFontDatabase::systemFont(QFontDatabase::GeneralFont);if(font.pointSizeF()<10.5)font.setPointSizeF(10.5);box.setFont(font);
        QColor outline=colors.windowText().color();outline.setAlpha(40);
        const QString border=QString("rgba(%1,%2,%3,%4)").arg(outline.red()).arg(outline.green()).arg(outline.blue()).arg(outline.alpha());
        box.setStyleSheet(QString(
            "QMessageBox { background:%1; color:%2; } QLabel { background:transparent; color:%2; }"
            "QPushButton { padding:7px 14px; border-radius:7px; border:1px solid %3; background:%4; color:%2; }"
            "QPushButton:hover { border-color:%5; } QPushButton:default { background:%5; color:%6; }")
            .arg(colors.window().color().name(),colors.windowText().color().name(),border,colors.base().color().name(),colors.highlight().color().name(),colors.highlightedText().color().name()));
        if(owner&&owner->screen())box.setScreen(owner->screen());
        if(owner)connect(owner,&QObject::destroyed,&box,&QDialog::reject);
        QTimer::singleShot(0,&box,[&box]{box.raise();box.activateWindow();if(box.windowHandle())box.windowHandle()->requestActivate();});
        return box.exec();
    }
    void info(const QString &title,const QString &text){QMessageBox box(QMessageBox::Warning,title,text,QMessageBox::Ok);show(box);}
    void done(){deleteLater();}
    void result(const QString &status,bool trash){
        if(!owner){done();return;}
        if(!unchanged()){
            info("Ярлык изменился","Ярлык был изменён или заменён. Откройте его ещё раз. Ничего не удалено.");done();return;
        }
        if(trash){
            if(status!="missing"){
                info("Ярлык сохранён","Приложение снова доступно или его отсутствие не удалось подтвердить. Ярлык оставлен на рабочем столе.");done();return;
            }
            if(!QFile::moveToTrash(path))info("Корзина","Не удалось отправить ярлык в корзину. Файл сохранён.");
            done();return;
        }
        if(status=="missing"){
            QMessageBox box(QMessageBox::Warning,"Приложение не найдено",QString("Приложение, на которое указывает ярлык «%1», не найдено. Возможно, оно удалено или перемещено.\n\nОтправить только этот ярлык в корзину?").arg(name),QMessageBox::NoButton);
            box.setObjectName("MissingApplicationShortcut");
            auto *remove=box.addButton("В корзину",QMessageBox::DestructiveRole);remove->setObjectName("TrashShortcut");
            auto *keep=box.addButton("Оставить",QMessageBox::RejectRole);keep->setObjectName("KeepShortcut");
            box.setDefaultButton(keep);box.setEscapeButton(keep);show(box);
            if(box.clickedButton()==remove){startProbe(true);return;}
        }else if(status=="unavailable"){
            info("Приложение недоступно","Приложение сейчас недоступно. Если оно находится на другом диске, подключите диск и попробуйте снова. Ярлык сохранён.");
        }else{
            QString exec=command;exec.replace(QRegularExpression("%[fFuUdDnNickvm]"),"");exec=exec.trimmed();
            if(!exec.isEmpty())LauncherUtil::startDetachedWindowedCommand(exec);
            else LauncherUtil::startDetachedWindowedProgram("xdg-open",{path});
        }
        done();
    }
public:
    LaunchJob(const QString &p,QWidget *w):QObject(qApp),path(p),owner(w){setObjectName("DesktopAppLaunchJob");pending.insert(path);}
    ~LaunchJob()override{pending.remove(path);}
    void start(){if(!snapshot()){if(owner)info("Ярлык недоступен","Не удалось прочитать ярлык. Файл не удалён.");done();return;}startProbe();}
    void startProbe(bool trash=false){
        auto *process=new QProcess(this);
        auto handled=std::make_shared<bool>(false);
        const auto finish=[this,process,handled,trash](bool normal){
            if(*handled)return;*handled=true;
            const auto data=QJsonDocument::fromJson(process->readAllStandardOutput()).object();
            const QString status=normal&&process->exitCode()==0?data.value("status").toString():QString("unknown");
            process->deleteLater();result(status,trash);
        };
        connect(process,&QProcess::finished,this,[finish](int,QProcess::ExitStatus status){finish(status==QProcess::NormalExit);});
        connect(process,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish(false);});
        QTimer::singleShot(6000,process,[process]{if(process->state()!=QProcess::NotRunning)process->kill();});
        process->start("/usr/bin/python3",{"-I",QCoreApplication::applicationDirPath()+"/app_probe.py",path});
    }
};
}
void DesktopAppLauncher::open(const QString &path,QWidget *owner){
    const QString absolute=QFileInfo(path).absoluteFilePath();
    if(!owner||QFileInfo(path).suffix().compare("desktop",Qt::CaseInsensitive)!=0||pending.contains(absolute))return;
    (new LaunchJob(absolute,owner))->start();
}
