#pragma once
#include "pagesettings.h"
#include "pagecarousel.h"
#include "pagepager.h"
#include "pagecontrol.h"
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QContextMenuEvent>
#ifdef OMD_DOLPHIN_LAUNCH_CHECK
#include "dolphinintegration.h"
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KDesktopFile>
#endif

static int carouselPreview(){
    const QString directory=qEnvironmentVariable("OMD_CAROUSEL_PREVIEW_DIR");
    if(!directory.contains("properties-check"))return 234;
    QDir().mkpath(directory);
    auto *l=new LayoutTestScreen("HDMI-A-1",0);auto *r=new LayoutTestScreen("DP-1",1920);
    QWindowSystemInterface::handleScreenAdded(l);QWindowSystemInterface::handleScreenAdded(r);
    {
        PropertiesDialog d(PropertiesDialog::Settings{});d.selectPageMonitor("DP-1");d.setFixedHeight(740);
        d.findChild<QTabWidget*>()->setCurrentIndex(2);d.show();QTest::qWait(100);
        if(!d.grab().save(directory+"/one-page.png"))return 235;
        auto *carousel=d.findChild<PageCarousel*>("PageCarousel");
        d.findChild<QPushButton*>("PageAddLeft")->click();
        d.findChild<QPushButton*>("PageHome")->click();QTest::qWait(280);
        d.findChild<QPushButton*>("PageAddRight")->click();
        d.findChild<QPushButton*>("PageHome")->click();QTest::qWait(280);
        if(!d.grab().save(directory+"/three-pages.png"))return 236;
        carousel->setCurrentIndex(0);QTest::qWait(280);
        if(!d.grab().save(directory+"/edge-page.png"))return 237;
        d.reject();
    }
    QWindowSystemInterface::handleScreenRemoved(l);QWindowSystemInterface::handleScreenRemoved(r);
    return 0;
}

static int pagesCheck(bool reload) {
    const QString path=qEnvironmentVariable("OMD_DESKTOP_DIR");
    if(!path.contains("pages-check"))return 170;
    QDir().mkpath(path);
    const QString cfg=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if(!reload) {
        QFile file(path+"/legacy.txt");if(!file.open(QIODevice::WriteOnly))return 171;file.write("user bytes preserved\n");file.close();
        if(!QFile::link(path+"/../absent-volume/folder",path+"/offline-link"))return 172;
        QSettings seed(cfg+"/icon-positions-DP-1.ini",QSettings::IniFormat);seed.setValue("positions/legacy.txt",QPoint(524,339));seed.sync();
    }
    auto *l=new LayoutTestScreen("HDMI-A-1",0);auto *r=new LayoutTestScreen("DP-1",1920);
    QWindowSystemInterface::handleScreenAdded(l);QWindowSystemInterface::handleScreenAdded(r);
    const int result=[&]() -> int {
        DesktopScene left(l->screen()),right(r->screen());left.resize(1920,1080);right.resize(1920,1080);left.show();right.show();QTest::qWait(550);
        auto *model=DesktopLayout::instance();
        QSettings expected(path+"/../pages-expected.ini",QSettings::IniFormat);
        if(reload) {
            if(right.currentPage()!=expected.value("active").toString()||left.currentPage()!="main")return 173;
            auto *icon=layoutIcon(right,"legacy.txt");if(!icon||icon->pos()!=expected.value("position").toPoint())return 174;
            if(layoutIcon(left,"legacy.txt"))return 175;
            if(!right.setCurrentPage("main"))return 176;QTest::qWait(500);
            if(!layoutIcon(right,"Local page folder")||!layoutIcon(right,"offline-link"))return 177;
            if(model->pages("DP-1").size()!=2)return 178;
            qInfo("PASS separate-process reload: active page, order, monitor ownership, positions and deleted-page recovery");return 0;
        }
        auto *legacy=layoutIcon(right,"legacy.txt");if(!legacy||left.currentPage()!="main"||right.currentPage()!="main")return 179;
        const QPoint originalPosition=legacy->pos();
        if(!layoutIcon(right,"offline-link"))return 180;
        {
            // Exercise the visible editor before touching persistent page data.
            PageCarousel bounds;
            bounds.setPages({DesktopPage{"main","Основная"}},"main","main");
            for(int width:{360,420,496,640}){
                bounds.resize(width,270);bounds.show();QTest::qWait(20);
                const auto center=bounds.screenRect(0);
                for(const auto &name:{"PageAddLeft","PageAddRight"}){
                    auto *plus=bounds.findChild<QPushButton*>(name);
                    if(!plus->isVisible()||!bounds.rect().contains(plus->geometry()))return 239;
                    if(center.intersects(QRectF(plus->geometry())))return 240;
                }
            }
            bounds.hide();
            PageCarousel wheel;
            wheel.setPages({DesktopPage{"left","Левая"},DesktopPage{"main","Основная"},DesktopPage{"right","Правая"}},"main","main");
            wheel.resize(480,270);wheel.show();
            auto sendWheel=[](QWidget *widget,int angle,int pixels=0,Qt::KeyboardModifiers mods=Qt::NoModifier){
                const QPointF point(widget->rect().center());
                QWheelEvent event(point,point,QPoint(0,pixels),QPoint(0,angle),Qt::NoButton,mods,Qt::NoScrollPhase,false);
                QCoreApplication::sendEvent(widget,&event);
            };
            sendWheel(&wheel,120);if(wheel.selectedPage()!="right")return 241;
            sendWheel(&wheel,-120);if(wheel.selectedPage()!="main")return 242;
            sendWheel(&wheel,0,1);if(wheel.selectedPage()!="right")return 243;
            sendWheel(&wheel,0);if(wheel.selectedPage()!="right")return 244;
            sendWheel(&wheel,120);if(wheel.selectedPage()!="left")return 262;
            sendWheel(&wheel,-120);if(wheel.selectedPage()!="right")return 263;
            wheel.findChild<QPushButton*>("PageNext")->click();if(wheel.selectedPage()!="left")return 264;
            wheel.findChild<QPushButton*>("PagePrevious")->click();if(wheel.selectedPage()!="right")return 265;
            qunsetenv("OMD_NO_ANIMATIONS");sendWheel(&wheel,120);QTest::qWait(60);
            if(wheel.selectedPage()!="left"||wheel.screenRect(0).center().x()<=wheel.width()/2.)return 268;
            QTest::qWait(280);
            if(qAbs(wheel.screenRect(0).center().x()-wheel.width()/2.)>1)return 269;
            sendWheel(&wheel,-120);QTest::qWait(60);
            if(wheel.selectedPage()!="right"||wheel.screenRect(2).center().x()>=wheel.width()/2.)return 270;
            QTest::qWait(280);qputenv("OMD_NO_ANIMATIONS","1");
            PagePager wheelPager;wheelPager.setPages({DesktopPage{"main","Основная"}},"main","DP-1");
            QSignalSpy steps(&wheelPager,&PagePager::stepped);
            sendWheel(&wheelPager,120);sendWheel(&wheelPager,-120);sendWheel(&wheelPager,0,1);
            if(steps.count()!=3||steps[0][0].toInt()!=1||steps[1][0].toInt()!=-1||steps[2][0].toInt()!=1)return 245;
            wheel.hide();
            PageSettings visual;visual.selectMonitor("DP-1");visual.resize(480,500);visual.show();QTest::qWait(50);
            auto *carousel=visual.findChild<PageCarousel*>("PageCarousel");
            auto *addLeft=visual.findChild<QPushButton*>("PageAddLeft");
            auto *addRight=visual.findChild<QPushButton*>("PageAddRight");
            if(!carousel||carousel->selectedPage()!="main"||!addLeft->isVisible()||!addRight->isVisible())return 224;
            if(qAbs(carousel->screenRect(0).center().x()-carousel->width()/2.)>1)return 225;
            QTest::mouseClick(addLeft,Qt::LeftButton);QTest::qWait(50);
            const QString created=carousel->selectedPage();
            if(created=="main"||created.isEmpty()||!addLeft->isVisible()||addRight->isVisible())return 226;
            qunsetenv("OMD_NO_ANIMATIONS");
            QTest::mouseClick(visual.findChild<QPushButton*>("PageHome"),Qt::LeftButton);QTest::qWait(60);
            if(qAbs(carousel->screenRect(carousel->currentIndex()).center().x()-carousel->width()/2.)<1)return 238;
            QTest::qWait(280);qputenv("OMD_NO_ANIMATIONS","1");
            if(carousel->selectedPage()!="main"||addLeft->isVisible()||!addRight->isVisible())return 227;
            QTest::keyClick(carousel,Qt::Key_Left);QTest::qWait(280);
            if(carousel->selectedPage()!=created)return 228;
            QTest::keyClick(carousel,Qt::Key_Home);QTest::qWait(280);
            if(carousel->selectedPage()!="main")return 229;
            const QPoint origin=carousel->screenRect(carousel->currentIndex()).center().toPoint();
            QTest::mousePress(carousel,Qt::LeftButton,Qt::NoModifier,origin);
            QTest::mouseRelease(carousel,Qt::LeftButton,Qt::NoModifier,origin+QPoint(75,0));QTest::qWait(280);
            if(carousel->selectedPage()!=created)return 230;
            visual.selectMonitor("HDMI-A-1");QTest::qWait(50);
            if(carousel->selectedPage()!="main"||!addLeft->isVisible()||!addRight->isVisible())return 231;
            visual.selectMonitor("DP-1");QTest::qWait(50);
            if(carousel->selectedPage()!="main")return 232;
            // Destruction/cancel must discard both draft pages and draft names.
        }
        if(model->pages("DP-1").size()!=1)return 233;
        {PageSettings cancelled;cancelled.selectMonitor("DP-1");cancelled.findChild<QPushButton*>("PageAddLeft")->click();}
        if(model->pages("DP-1").size()!=1)return 181;
        PageSettings editor;editor.selectMonitor("DP-1");
        auto *list=editor.findChild<PageCarousel*>("PageCarousel");
        if(editor.findChild<QPushButton*>("PageRemove")->isEnabled())return 182;
        editor.findChild<QPushButton*>("PageAddLeft")->click();
        const QString leftId=list->selectedPage();
        list->setCurrentIndex(1);editor.findChild<QPushButton*>("PageAddRight")->click();
        const QString rightId=list->selectedPage();
        auto *name=editor.findChild<QLineEdit*>("PageName");name->setText("Работа");QMetaObject::invokeMethod(name,"textEdited",Q_ARG(QString,QString("Работа")));
        editor.findChild<QPushButton*>("PageMoveLeft")->click();editor.findChild<QPushButton*>("PageMoveRight")->click();
        if(!editor.commit())return 183;QTest::qWait(500);
        if(model->pages("DP-1").size()!=3||model->pages("DP-1")[0].id!=leftId||model->pages("DP-1")[2].id!=rightId||model->pages("DP-1")[2].name!="Работа")return 184;
        auto desktopWheel=[&](int delta){
            QWheelEvent event(QPointF(500,500),QPointF(500,500),QPoint(),QPoint(0,delta),Qt::NoButton,Qt::MetaModifier|Qt::ShiftModifier,Qt::NoScrollPhase,false);
            QCoreApplication::sendEvent(&right,&event);QTest::qWait(35);
        };
        QWheelEvent otherBinding(QPointF(500,500),QPointF(500,500),QPoint(),QPoint(0,120),Qt::NoButton,Qt::MetaModifier|Qt::AltModifier|Qt::ShiftModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(&right,&otherBinding);QTest::qWait(35);
        if(right.currentPage()!="main")return 251;
        desktopWheel(120);if(right.currentPage()!=rightId||left.currentPage()!="main")return 246;
        desktopWheel(120);if(right.currentPage()!=leftId||left.currentPage()!="main")return 266;
        desktopWheel(-120);if(right.currentPage()!=rightId||left.currentPage()!="main")return 267;
        desktopWheel(-120);if(right.currentPage()!="main")return 247;
        if(!layoutIcon(right,"legacy.txt")||layoutIcon(right,"legacy.txt")->pos()!=originalPosition)return 185;
        if(!right.moveIconsToPage(&right,{"legacy.txt"},rightId))return 186;QTest::qWait(450);
        if(layoutIcon(right,"legacy.txt"))return 187;
        if(!right.setCurrentPage(rightId))return 188;QTest::qWait(500);
        if(!layoutIcon(right,"legacy.txt")||layoutIcon(right,"offline-link")||left.currentPage()!="main") {
            fprintf(stderr,"189: right=%s expected=%s entry=%s legacy=%p offline=%p left=%s\n",qPrintable(right.currentPage()),qPrintable(rightId),qPrintable(model->entryPage("legacy.txt")),(void*)layoutIcon(right,"legacy.txt"),(void*)layoutIcon(right,"offline-link"),qPrintable(left.currentPage()));
            for(auto *icon:right.findChildren<DesktopIcon*>())fprintf(stderr,"child %s hidden=%d virtual=%d\n",qPrintable(icon->fileInfo().fileName()),icon->isHidden(),icon->isVirtual());
            return 189;
        }
        QTimer::singleShot(0,[]{auto *d=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());if(d){d->setTextValue("Local page folder");d->accept();}});
        QMetaObject::invokeMethod(&right,"onNewFolder",Qt::DirectConnection);QTest::qWait(500);
        if(!layoutIcon(right,"Local page folder")||model->entryPage("Local page folder")!=rightId)return 190;
        // Context creation stays on the clicked output, independent of focus.
        QFile appFile(path+"/../fixture.desktop");if(!appFile.open(QIODevice::WriteOnly))return 191;
        appFile.write("[Desktop Entry]\nType=Application\nName=Fixture App\nExec=true\nIcon=application-x-executable\n");appFile.close();
        const QPoint click(780,450);
        QContextMenuEvent context(QContextMenuEvent::Keyboard,click,left.mapToGlobal(click));QCoreApplication::sendEvent(&left,&context);
        model->setFocusedScreen("DP-1");
        QTimer::singleShot(0,[&]{auto *picker=qobject_cast<PropertiesDialog*>(QApplication::activeModalWidget());if(picker){QMetaObject::invokeMethod(picker,"applicationShortcutRequested",Qt::DirectConnection,Q_ARG(QString,appFile.fileName()));picker->reject();}});
        QMetaObject::invokeMethod(&left,"onApplicationShortcut",Qt::DirectConnection);QTest::qWait(500);
        if(model->owner("Fixture App.desktop")!="HDMI-A-1"||model->entryPage("Fixture App.desktop")!="main"||layoutIcon(right,"Fixture App.desktop"))return 192;
        if(!layoutIcon(left,"Fixture App.desktop")||(layoutIcon(left,"Fixture App.desktop")->pos()-click).manhattanLength()>160)return 271;
        // A full properties dialog follows focus at the moment of Create.
        QFile propsApp(path+"/../properties-fixture.desktop");if(!propsApp.open(QIODevice::WriteOnly))return 272;
        propsApp.write("[Desktop Entry]\nType=Application\nName=Properties Fixture\nExec=true\nIcon=application-x-executable\n");propsApp.close();
        model->setFocusedScreen("HDMI-A-1");
        QTimer::singleShot(0,[&]{
            model->setFocusedScreen("DP-1");
            auto *dialog=qobject_cast<PropertiesDialog*>(QApplication::activeModalWidget());
            if(dialog){QMetaObject::invokeMethod(dialog,"applicationShortcutRequested",Qt::DirectConnection,Q_ARG(QString,propsApp.fileName()));dialog->reject();}
        });
        QMetaObject::invokeMethod(&left,"onShowProperties",Qt::DirectConnection);QTest::qWait(500);
        if(model->owner("Properties Fixture.desktop")!="DP-1"||model->entryPage("Properties Fixture.desktop")!=rightId||!layoutIcon(right,"Properties Fixture.desktop")||layoutIcon(left,"Properties Fixture.desktop"))return 273;
        auto sets=model->pageSets();sets["HDMI-A-1"].append(DesktopPage{"left-extra","Левая страница"});if(!model->replacePages(sets))return 193;
        if(!left.setCurrentPage("left-extra"))return 194;QTest::qWait(500);
        QFile pinnedApp(path+"/../pinned-fixture.desktop");if(!pinnedApp.open(QIODevice::WriteOnly))return 274;
        pinnedApp.write("[Desktop Entry]\nType=Application\nName=Pinned Context\nExec=true\nIcon=application-x-executable\n");pinnedApp.close();
        const QPoint pinnedPoint(1280,720);
        QContextMenuEvent pinnedMenu(QContextMenuEvent::Keyboard,pinnedPoint,left.mapToGlobal(pinnedPoint));QCoreApplication::sendEvent(&left,&pinnedMenu);
        QTimer::singleShot(0,[&]{
            left.setCurrentPage("main");
            QTimer::singleShot(150,[&]{auto *picker=qobject_cast<PropertiesDialog*>(QApplication::activeModalWidget());if(picker){QMetaObject::invokeMethod(picker,"applicationShortcutRequested",Qt::DirectConnection,Q_ARG(QString,pinnedApp.fileName()));picker->reject();}});
        });
        QMetaObject::invokeMethod(&left,"onApplicationShortcut",Qt::DirectConnection);QTest::qWait(500);
        if(left.currentPage()!="main"||model->owner("Pinned Context.desktop")!="HDMI-A-1"||model->entryPage("Pinned Context.desktop")!="left-extra"||layoutIcon(left,"Pinned Context.desktop"))return 275;
        if(!left.setCurrentPage("left-extra"))return 276;QTest::qWait(500);
        if(!layoutIcon(left,"Pinned Context.desktop")||(layoutIcon(left,"Pinned Context.desktop")->pos()-pinnedPoint).manhattanLength()>160)return 277;
        if(!left.receiveDesktopDrop(&right,{"legacy.txt"},QPoint(800,400)))return 195;QTest::qWait(500);
        if(!layoutIcon(left,"legacy.txt")||layoutIcon(right,"legacy.txt")||model->entryPage("legacy.txt")!="left-extra")return 196;
        if(!right.moveIconsToPage(&left,{"legacy.txt"},leftId))return 197;QTest::qWait(500);
        if(!right.setCurrentPage(leftId)||!left.setCurrentPage("main"))return 198;QTest::qWait(500);
        if(!layoutIcon(right,"legacy.txt")||layoutIcon(left,"legacy.txt"))return 199;
        if(right.moveIconsToPage(&right,{"legacy.txt","not-present"},"main")||right.setCurrentPage("not-present"))return 200;
        sets=model->pageSets();auto invalid=sets;invalid["DP-1"]={{"x","No main"}};if(model->replacePages(invalid))return 201;
        // Delete the page that owns the newly created folder; return it to main.
        for(int n=0;n<sets["DP-1"].size();++n)if(sets["DP-1"][n].id==rightId){sets["DP-1"].removeAt(n);break;}
        if(!model->replacePages(sets))return 202;QTest::qWait(500);
        if(model->entryPage("Local page folder")!="main"||!QFileInfo(path+"/Local page folder").isDir())return 203;
        // Reorder pages while active; stable IDs keep the visible icon in place.
        auto *icon=layoutIcon(right,"legacy.txt");if(!icon)return 204;const QPoint position=icon->pos();
        sets["DP-1"].move(0,1);if(!model->replacePages(sets))return 205;QTest::qWait(500);
        if(right.currentPage()!=leftId||!layoutIcon(right,"legacy.txt")||layoutIcon(right,"legacy.txt")->pos()!=position)return 206;
        // Exercise an actual 240 ms transition and ensure no timer survives it.
        qunsetenv("OMD_NO_ANIMATIONS");if(!right.setCurrentPage("main"))return 207;QTest::qWait(70);
        if(!right.findChild<QVariantAnimation*>())return 208;
        if(qAlpha(right.grab().toImage().pixel(1800,800))!=0)return 222;
        if(!right.setCurrentPage(leftId))return 209;QTest::qWait(650);
        for(auto *anim:right.findChildren<QVariantAnimation*>())if(anim->state()==QAbstractAnimation::Running)return 210;
        qputenv("OMD_NO_ANIMATIONS","1");
        if(qAlpha(right.grab().toImage().pixel(1800,800))!=0)return 223;
        QTemporaryDir socketDir("/tmp/omd-pages-check-XXXXXX");if(!socketDir.isValid())return 214;
        if(!qEnvironmentVariableIsSet("OMD_CONTROL_SOCKET"))qputenv("OMD_CONTROL_SOCKET",QFile::encodeName(socketDir.path()+"/pages.sock"));
        PageControl control;if(!control.listen())return 214;
        {PageControl duplicate;if(duplicate.listen())return 215;}
        auto client=[&](const QString &command,const QString &monitor)->bool {
            QProcess process;process.start("/usr/bin/python3",{"-I",QCoreApplication::applicationDirPath()+"/desktop_pages.py",command,monitor});
            QSignalSpy finished(&process,&QProcess::finished);
            if(!finished.wait(4000))return false;
            return process.exitStatus()==QProcess::NormalExit&&process.exitCode()==0;
        };
        if(!client("previous","DP-1"))return 216;QTest::qWait(500);
        if(right.currentPage()!="main"||left.currentPage()!="main")return 217;
        if(!client("next","DP-1"))return 218;QTest::qWait(500);
        if(right.currentPage()!=leftId||left.currentPage()!="main")return 219;
        if(client("next","not-a-monitor")||client("bad-command","DP-1"))return 220;
        if(!client("status","HDMI-A-1"))return 221;
        model->setFocusedScreen("HDMI-A-1");if(!client("status",""))return 248;
        model->setFocusedScreen("not-connected");if(model->focusedScreen()!="HDMI-A-1")return 249;
        model->setFocusedScreen("DP-1");if(!client("status",""))return 250;
        // End-to-end: the real helper asks the running app for the active target.
        const QString origin=path+"/../dolphin-origin";QDir().mkpath(origin);
        QFile data(origin+"/data.txt");if(!data.open(QIODevice::WriteOnly))return 252;data.write("original folder data\n");data.close();
        QFile dirs(qEnvironmentVariable("XDG_CONFIG_HOME")+"/user-dirs.dirs");
        if(!dirs.open(QIODevice::WriteOnly))return 253;
        dirs.write(("XDG_DESKTOP_DIR=\""+path+"\"\n").toUtf8());dirs.close();
        model->setFocusedScreen("HDMI-A-1");if(!left.setCurrentPage("left-extra"))return 254;QTest::qWait(100);
        QProcess sendLink;sendLink.start("/usr/bin/python3",{"-I",QCoreApplication::applicationDirPath()+"/send-to-desktop.py","--quiet","--",origin});
        QSignalSpy linkFinished(&sendLink,&QProcess::finished);if(!linkFinished.wait(5000))return 255;
        if(sendLink.exitCode()!=0)return 256;QTest::qWait(400);
        const QString linkName="dolphin-origin — ярлык";
        if(!QFileInfo(path+"/"+linkName).isSymLink()||model->owner(linkName)!="HDMI-A-1"||model->entryPage(linkName)!="left-extra")return 257;
        if(!layoutIcon(left,linkName)||layoutIcon(right,linkName))return 258;
        if(right.placeDesktopLinks({"../dolphin-origin"},"main")||right.placeDesktopLinks({"legacy.txt"},"main")||right.placeDesktopLinks({linkName},"not-present"))return 259;
        if(!data.open(QIODevice::ReadOnly)||data.readAll()!="original folder data\n")return 260;data.close();
#ifdef OMD_DOLPHIN_LAUNCH_CHECK
        // Execute the real KDE action as Dolphin does, not only its Python helper.
        // CTest uses a private D-Bus session, so test notifications stay isolated.
        DolphinIntegration integration;if(!integration.activate())return 270;
        if(!KDesktopFile::isAuthorizedDesktopFile(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/kio/servicemenus/oh-my-desktop-link.desktop"))return 271;
        KFileItemList items;items<<KFileItem(QUrl::fromLocalFile(origin));
        KFileItemActions builder;builder.setItemListProperties(KFileItemListProperties(items));
        QAction *desktopAction=nullptr;
        const auto serviceActions=builder.createServiceMenuActions();
        for(auto *action:serviceActions)if(action->text()==QString::fromUtf8("Создать ярлык на рабочем столе"))desktopAction=action;
        if(!desktopAction)return 272;
        desktopAction->trigger();
        const QString secondLink="dolphin-origin — ярлык (2)";
        for(int wait=0;wait<100 && (!QFileInfo(path+"/"+secondLink).isSymLink()||model->entryPage(secondLink)!="left-extra"||!layoutIcon(left,secondLink));++wait)QTest::qWait(50);
        if(!QFileInfo(path+"/"+secondLink).isSymLink()||model->owner(secondLink)!="HDMI-A-1"||model->entryPage(secondLink)!="left-extra"){fprintf(stderr,"KDE action result: %s; owner=%s page=%s\n",qPrintable(QDir(path).entryList().join(" | ")),qPrintable(model->owner(secondLink)),qPrintable(model->entryPage(secondLink)));return 273;}
        if(!layoutIcon(left,secondLink)||layoutIcon(right,secondLink))return 274;
        if(!data.open(QIODevice::ReadOnly)||data.readAll()!="original folder data\n")return 275;data.close();
        integration.deactivate();
        qInfo("PASS actual KDE service-menu action launches and places a link on the active monitor/page");
#endif
        if(!left.setCurrentPage("main"))return 261;model->setFocusedScreen("DP-1");QTest::qWait(300);
        auto *pager=right.findChild<PagePager*>();if(!pager||!pager->isVisible()||pager->geometry().center().x()<850)return 211;
        for(auto *i:right.findChildren<DesktopIcon*>())if(!i->isHidden()&&i->geometry().intersects(pager->geometry()))return 212;
        PropertiesDialog preview(right.desktopSettings());preview.selectPageMonitor("DP-1");preview.findChild<QTabWidget*>()->setCurrentIndex(2);preview.show();QTest::qWait(100);preview.grab().save(path+"/../pages-settings.png");preview.reject();
        pager->grab().save(path+"/../page-pager.png");
        expected.setValue("active",leftId);expected.setValue("position",layoutIcon(right,"legacy.txt")->pos());expected.sync();
        QFile original(path+"/legacy.txt");if(!original.open(QIODevice::ReadOnly)||original.readAll()!="user bytes preserved\n"||!QFileInfo(path+"/offline-link").isSymLink())return 213;
        qInfo("PASS context app shortcut at click, pinned page during picker, properties app shortcut on focused monitor/active page, per-monitor persistence and original bytes");
        return 0;
    }();
    QWindowSystemInterface::handleScreenRemoved(l);QWindowSystemInterface::handleScreenRemoved(r);return result;
}
