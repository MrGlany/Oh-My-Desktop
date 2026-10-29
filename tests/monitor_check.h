#pragma once
#include "desktoplayout.h"
#include <QInputDialog>
#include <QTextStream>

class LayoutTestScreen : public QPlatformScreen {
public:
    LayoutTestScreen(QString name, int x) : m_name(name), m_x(x) {}
    QRect geometry() const override { return QRect(m_x,0,1920,1080); }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_ARGB32_Premultiplied; }
    QString name() const override { return m_name; }
private:
    QString m_name; int m_x;
};

static DesktopIcon *layoutIcon(DesktopScene &scene, const QString &key) {
    for (auto *icon : scene.findChildren<DesktopIcon*>(QString(), Qt::FindDirectChildrenOnly))
        if (!icon->isHidden() && (icon->isVirtual() ? key=="__trash__" && icon->virtualType()==DesktopIcon::VirtualType::Trash : icon->fileInfo().fileName()==key)) return icon;
    return nullptr;
}
static int monitorCheck(bool reload) {
    if (!qEnvironmentVariable("OMD_DESKTOP_DIR").contains("monitor-check")) return 100;
    auto *leftScreen = new LayoutTestScreen("HDMI-A-1", 0);
    auto *rightScreen = new LayoutTestScreen("DP-1", 1920);
    QWindowSystemInterface::handleScreenAdded(leftScreen);
    QWindowSystemInterface::handleScreenAdded(rightScreen);
    const QString path=qEnvironmentVariable("OMD_DESKTOP_DIR");
    QDir().mkpath(path);
    if (!reload) {
        QFile f(path+"/external.txt");if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 101;f.write("preserve me\n");
    }
    int result = [&]() -> int {
        DesktopScene left(leftScreen->screen()),right(rightScreen->screen());
        left.resize(1920,1080);right.resize(1920,1080);left.show();right.show();QTest::qWait(650);
        if(reload) {
            if(!layoutIcon(left,"external.txt") || layoutIcon(right,"external.txt"))return 102;
            if(!layoutIcon(left,"Левая папка") || layoutIcon(right,"Левая папка"))return 103;
            if(!layoutIcon(left,"__trash__") || layoutIcon(right,"__trash__"))return 104;
            if(!layoutIcon(right,"Fixture App.desktop") || layoutIcon(left,"Fixture App.desktop"))return 105;
            QSettings expected(path+"/../expected.ini",QSettings::IniFormat);
            if(layoutIcon(left,"external.txt")->pos()!=expected.value("position").toPoint())return 118;
            printf("PASS separate process restores per-monitor ownership and positions\n");
            return 0;
        }
        if(layoutIcon(left,"external.txt") || !layoutIcon(right,"external.txt"))return 106;
        if(layoutIcon(left,"__trash__") || !layoutIcon(right,"__trash__"))return 107;
        QTimer::singleShot(0, []{
            auto *d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if(d){d->setTextValue("Левая папка");d->accept();}
        });
        QMetaObject::invokeMethod(&left,"onNewFolder",Qt::DirectConnection);
        QTest::qWait(650);
        if(!layoutIcon(left,"Левая папка") || layoutIcon(right,"Левая папка"))return 108;
        QFile appFile(path+"/../fixture.desktop");
        if(!appFile.open(QIODevice::WriteOnly))return 109;
        appFile.write("[Desktop Entry]\nType=Application\nName=Fixture App\nExec=true\nIcon=application-x-executable\n");appFile.close();
        QTimer::singleShot(0,[&]{
            auto *picker=qobject_cast<PropertiesDialog*>(QApplication::activeModalWidget());
            if(picker){QMetaObject::invokeMethod(picker,"applicationShortcutRequested",Qt::DirectConnection,Q_ARG(QString,appFile.fileName()));picker->reject();}
        });
        QMetaObject::invokeMethod(&left,"onApplicationShortcut",Qt::DirectConnection);
        QTest::qWait(650);
        if(layoutIcon(left,"Fixture App.desktop") || !layoutIcon(right,"Fixture App.desktop"))return 110;
        if(!left.receiveDesktopDrop(&right,{"external.txt","__trash__"},QPoint(700,400)))return 111;
        QTest::qWait(650);
        if(!layoutIcon(left,"external.txt") || layoutIcon(right,"external.txt"))return 112;
        if(!layoutIcon(left,"__trash__") || layoutIcon(right,"__trash__"))return 113;
        if(!right.receiveDesktopDrop(&left,{"external.txt"},QPoint(1000,500)))return 119;
        QTest::qWait(650);
        if(layoutIcon(left,"external.txt") || !layoutIcon(right,"external.txt"))return 120;
        if(!left.receiveDesktopDrop(&right,{"external.txt"},QPoint(700,400)))return 121;
        QTest::qWait(650);
        QFile source(path+"/../symlink-target.txt");
        if(!source.open(QIODevice::WriteOnly))return 123;
        source.write("original target\n");source.close();
        if(!QFile::link(source.fileName(),path+"/external-link"))return 124;
        QTest::qWait(650);
        if(!left.receiveDesktopDrop(&right,{"external-link"},QPoint(1000,600)))return 125;
        QTest::qWait(650);
        if(!QFileInfo(path+"/external-link").isSymLink() || !QFileInfo::exists(source.fileName()))return 126;
        QFile reused(path+"/reused.txt");if(!reused.open(QIODevice::WriteOnly))return 127;reused.close();
        QTest::qWait(650);
        if(!left.receiveDesktopDrop(&right,{"reused.txt"},QPoint(1000,750)))return 128;
        QTest::qWait(650);
        if(!reused.remove())return 129;
        QTest::qWait(650);
        if(!reused.open(QIODevice::WriteOnly))return 130;reused.close();
        QTest::qWait(650);
        if(layoutIcon(left,"reused.txt") || !layoutIcon(right,"reused.txt"))return 131;
        if(right.receiveDesktopDrop(&left,{"external.txt","not-a-real-icon"},QPoint(500,300)))return 114;
        auto settings=left.desktopSettings();settings.iconSize=64;
        left.applyDesktopSettings(settings,true);QTest::qWait(650);
        if(!layoutIcon(left,"external.txt") || layoutIcon(right,"external.txt") || !layoutIcon(left,"__trash__") || layoutIcon(right,"__trash__"))return 115;
        QFile f(path+"/external.txt");if(!f.open(QIODevice::ReadOnly) || f.readAll()!="preserve me\n")return 116;
        QSettings expected(path+"/../expected.ini",QSettings::IniFormat);
        expected.setValue("position",layoutIcon(left,"external.txt")->pos());expected.sync();
        printf("PASS one copy per output, local folder, main-output app shortcut, multi-icon transfer, invalid drop, resize, file preservation\n");
        return 0;
    }();
    QWindowSystemInterface::handleScreenRemoved(leftScreen);
    if(!result && !reload) {
        DesktopScene fallback(rightScreen->screen());fallback.resize(1920,1080);fallback.show();QTest::qWait(650);
        if(!layoutIcon(fallback,"external.txt") || !layoutIcon(fallback,"__trash__"))result=122;
        if(!result)printf("PASS unplugged output temporarily falls back without overwriting its saved owner\n");
    }
    QWindowSystemInterface::handleScreenRemoved(rightScreen);
    return result;
}
