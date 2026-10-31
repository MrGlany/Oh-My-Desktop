#pragma once
#include "pagesettings.h"
#include <QComboBox>

static int primaryMonitorCheck(bool reload){
    const QString path=qEnvironmentVariable("OMD_DESKTOP_DIR");
    if(!path.contains("primary-monitor-check"))return 11;
    QDir().mkpath(path);
    qputenv("OMD_PRIMARY_MONITOR","DP-1");
    auto *leftScreen=new LayoutTestScreen("HDMI-A-1",0);
    auto *rightScreen=new LayoutTestScreen("DP-1",1920);
    QWindowSystemInterface::handleScreenAdded(leftScreen);QWindowSystemInterface::handleScreenAdded(rightScreen);
    auto *model=DesktopLayout::instance();
    auto require=[](bool ok,const char *message){if(!ok)qCritical("FAIL primary monitor: %s",message);return ok;};
    if(reload){
        if(!require(model->preferredPrimaryScreen()=="HDMI-A-1"&&model->primaryScreen()=="HDMI-A-1","saved choice survives a new process and overrides env"))return 12;
    }else{
        if(!require(model->preferredPrimaryScreen().isEmpty()&&model->primaryScreen()=="DP-1","legacy default preserved"))return 13;
        QFile file(path+"/existing.txt");if(!file.open(QIODevice::WriteOnly))return 14;file.write("original bytes\n");file.close();
    }
    const int result=[&]()->int{
        DesktopScene left(leftScreen->screen()),right(rightScreen->screen());
        left.resize(1920,1080);right.resize(1920,1080);left.show();right.show();QTest::qWait(600);
        if(reload){
            if(!require(layoutIcon(right,"existing.txt")&&layoutIcon(left,"new-file.txt")&&right.currentPage()=="keep","ownership and active page survive restart"))return 15;
            QSettings expected(path+"/../expected.ini",QSettings::IniFormat);
            if(!require(layoutIcon(right,"existing.txt")->pos()==expected.value("position").toPoint(),"icon coordinates survive restart"))return 16;
            PageSettings editor;
            auto *choice=editor.findChild<QComboBox*>("PrimaryMonitor");
            if(!require(choice&&choice->currentData().toString()=="HDMI-A-1","editor loads saved choice"))return 17;
            choice->setCurrentIndex(choice->findData(QString()));
            if(!editor.commit()||!require(model->preferredPrimaryScreen().isEmpty()&&model->primaryScreen()=="DP-1","automatic mode restores legacy selection"))return 18;
            return 0;
        }
        auto sets=model->pageSets();sets["DP-1"].append(DesktopPage{"keep","Работа"});
        if(!model->replacePages(sets)||!model->assign({"existing.txt"},"DP-1","keep")||!model->activatePage("DP-1","keep"))return 19;
        QTest::qWait(500);
        auto *existing=layoutIcon(right,"existing.txt");if(!existing)return 20;
        const QPoint original=existing->pos();
        {PageSettings cancelled;auto *choice=cancelled.findChild<QComboBox*>("PrimaryMonitor");if(!choice)return 21;choice->setCurrentIndex(choice->findData("HDMI-A-1"));}
        if(!require(model->primaryScreen()=="DP-1"&&model->preferredPrimaryScreen().isEmpty(),"cancel leaves preference unchanged"))return 22;
        PageSettings editor;editor.selectMonitor("DP-1");
        auto *choice=editor.findChild<QComboBox*>("PrimaryMonitor");
        choice->setCurrentIndex(choice->findData("HDMI-A-1"));
        if(!require(editor.findChild<QComboBox*>("PageMonitor")->currentData().toString()=="DP-1","page editor output remains independent"))return 23;
        if(!editor.commit())return 24;QTest::qWait(500);
        if(!require(model->primaryScreen()=="HDMI-A-1"&&model->preferredPrimaryScreen()=="HDMI-A-1","apply changes primary"))return 25;
        if(!require(model->owner("existing.txt")=="DP-1"&&model->entryPage("existing.txt")=="keep"&&right.currentPage()=="keep"&&layoutIcon(right,"existing.txt")&&layoutIcon(right,"existing.txt")->pos()==original,"existing assignments, active page and coordinates unchanged"))return 26;
        QFile added(path+"/new-file.txt");if(!added.open(QIODevice::WriteOnly))return 27;added.write("new bytes\n");added.close();QTest::qWait(600);
        if(!require(layoutIcon(left,"new-file.txt")&&!layoutIcon(right,"new-file.txt"),"external new file appears on selected primary"))return 28;
        auto invalid=model->pageSets();invalid["DP-1"]={{"bad","No main"}};
        if(!require(!model->replacePages(invalid,"DP-1")&&!model->replacePages(model->pageSets(),"not-a-monitor")&&model->preferredPrimaryScreen()=="HDMI-A-1","invalid combined update preserves primary"))return 29;
        QSettings expected(path+"/../expected.ini",QSettings::IniFormat);expected.setValue("position",original);expected.sync();
        return 0;
    }();
    if(result)return result;
    if(!reload){
        QWindowSystemInterface::handleScreenRemoved(leftScreen);leftScreen=nullptr;
        if(!require(model->primaryScreen()=="DP-1"&&model->preferredPrimaryScreen()=="HDMI-A-1","unplug uses fallback without forgetting preferred monitor"))return 30;
        leftScreen=new LayoutTestScreen("HDMI-A-1",0);QWindowSystemInterface::handleScreenAdded(leftScreen);
        if(!require(model->primaryScreen()=="HDMI-A-1","reconnect restores selected primary"))return 31;
        QFile unchanged(path+"/existing.txt");if(!unchanged.open(QIODevice::ReadOnly)||unchanged.readAll()!=QByteArray("original bytes\n"))return 32;
    }
    QWindowSystemInterface::handleScreenRemoved(leftScreen);QWindowSystemInterface::handleScreenRemoved(rightScreen);
    qInfo("PASS primary monitor: apply/cancel, ownership, new files, unplug/reconnect and persistence");
    return 0;
}
