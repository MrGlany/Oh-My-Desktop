#pragma once
#include "applicationicons.h"
#include "filepropertiesdialog.h"
#include <QFileDialog>
#include <QDialogButtonBox>

static int applicationIconsCheck(){
    const QString root=qEnvironmentVariable("XDG_CONFIG_HOME")+"/application-icons-fixture";
    if(!root.contains("properties-check-application-icons"))return 144;
    const QString bundle=root+"/bundle",target=bundle+"/Fixture App",link=root+"/Shortcut";
    QDir().mkpath(bundle+"/share/applications");QDir().mkpath(bundle+"/share/icons/hicolor/1024x1024/apps");
    auto write=[](const QString &path,const QByteArray &bytes){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size();};
    const QString large=bundle+"/share/icons/hicolor/1024x1024/apps/org.fixture.App.png",custom=root+"/custom.png";
    QImage original(1024,1024,QImage::Format_ARGB32);original.fill(Qt::red);if(!original.save(large))return 145;
    QImage blue(256,256,QImage::Format_ARGB32);blue.fill(Qt::blue);if(!blue.save(custom))return 146;
    if(!write(target,"#!/bin/sh\nexit 99\n")||!QFile::setPermissions(target,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)||!QFile::link(target,link))return 147;
    if(!write(bundle+"/share/applications/org.fixture.App.desktop","[Desktop Entry]\nType=Application\nName=Fixture App\nExec=fixture-app\nIcon=org.fixture.App\n"))return 148;
    auto *icons=ApplicationIcons::instance();
    auto ready=[&](const QString &path){
        QIcon icon=icons->icon(QFileInfo(path));
        for(int wait=0;wait<150&&icon.isNull();++wait){QTest::qWait(20);icon=icons->icon(QFileInfo(path));}
        return icon;
    };
    auto automatic=ready(link);if(automatic.isNull()||!automatic.availableSizes().contains(QSize(1024,1024))||automatic.pixmap(96,96).toImage().pixelColor(48,48)!=QColor(Qt::red))return 149;
    if(!icons->setOverride(QFileInfo(link),custom)||ready(link).pixmap(96,96).toImage().pixelColor(48,48)!=QColor(Qt::blue))return 150;
    if(icons->setOverride(QFileInfo(link),target)||icons->overridePath(QFileInfo(link))!=custom)return 151;
    if(icons->icon(QFileInfo(link),true).pixmap(96,96).toImage().pixelColor(48,48)!=QColor(Qt::red))return 152;
    const QString renamed=root+"/Renamed";if(!QFile::rename(link,renamed))return 153;icons->moveOverride(link,renamed);
    if(icons->overridePath(QFileInfo(renamed))!=custom)return 154;
    if(!QFile::rename(renamed,renamed+".old")||!QFile::link(target,renamed)||!icons->overridePath(QFileInfo(renamed)).isEmpty())return 155;
    // Custom image selection must not modify the symlink's original .desktop.
    const QString source=root+"/Original.desktop",shortcut=root+"/App.desktop";
    const QByteArray entry="[Desktop Entry]\nType=Application\nName=Fixture\nExec=true\nIcon=application-x-executable\n";
    if(!write(source,entry)||!QFile::link(source,shortcut))return 156;
    auto choose=[&](FilePropertiesDialog &dialog,const QString &image){
        bool selected=false;QTimer timer;timer.setInterval(100);
        QObject::connect(&timer,&QTimer::timeout,[&]{
            auto *picker=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if(picker){
                if(auto *edit=picker->findChild<QLineEdit*>("fileNameEdit"))edit->setText(image);
                QMetaObject::invokeMethod(picker,"accept",Qt::DirectConnection);selected=picker->result()==QDialog::Accepted;
            }
        });
        QTimer::singleShot(3000,&timer,[&]{if(auto *d=qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))d->reject();});
        timer.start();dialog.findChild<QPushButton*>("ChooseDesktopIcon")->click();timer.stop();return selected;
    };
    {
        FilePropertiesDialog dialog{QFileInfo(shortcut)};dialog.show();
        if(!choose(dialog,custom))return 157;
        dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();dialog.reject();
    }
    if(icons->overridePath(QFileInfo(shortcut))!=custom||!QFileInfo(shortcut).isSymLink())return 158;
    {
        FilePropertiesDialog dialog{QFileInfo(shortcut)};dialog.show();
        if(!choose(dialog,large))return 159;dialog.reject();
    }
    if(icons->overridePath(QFileInfo(shortcut))!=custom)return 160;
    {
        FilePropertiesDialog dialog{QFileInfo(shortcut)};dialog.show();dialog.findChild<QPushButton*>("ResetDesktopIcon")->click();dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();dialog.reject();
    }
    if(!icons->overridePath(QFileInfo(shortcut)).isEmpty())return 161;
    QFile sourceAfter(source);if(!sourceAfter.open(QIODevice::ReadOnly)||sourceAfter.readAll()!=entry||!QFileInfo(shortcut).isSymLink())return 162;
    QFile executableAfter(target);if(!executableAfter.open(QIODevice::ReadOnly)||executableAfter.readAll()!="#!/bin/sh\nexit 99\n")return 163;
    const QString real=qEnvironmentVariable("OMD_REAL_ICON_CHECK_PATH");
    if(!real.isEmpty()){
        QFile before(real);if(!before.open(QIODevice::ReadOnly))return 164;const QByteArray bytes=before.readAll();before.close();
        const QString realLink=root+"/AniHot App";if(!QFile::link(real,realLink))return 165;
        const auto icon=ready(realLink);if(icon.isNull())return 166;
        bool largeEnough=false;for(const auto &size:icon.availableSizes())if(size.width()>=512)largeEnough=true;
        if(!largeEnough)return 167;
        DesktopIcon widget(QFileInfo(realLink),64,true);widget.show();QTest::qWait(100);widget.grab().save(root+"/real-app.png");
        QFile after(real);if(!after.open(QIODevice::ReadOnly)||after.readAll()!=bytes||!QFileInfo(realLink).isSymLink())return 168;
        puts("PASS real application bundle: high-resolution icon rendered through a temporary symlink; original binary unchanged");
    }
    puts("PASS 1024px bundle icon through symlink, manual image, automatic reset, rename, replacement identity, Apply/Cancel, original launcher and executable preserved");return 0;
}
