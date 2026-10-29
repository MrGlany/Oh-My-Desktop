#include "inlinemenu.h"
#include <QApplication>
#include <QTest>
#include <QSettings>
#include <QDir>
#include <QPainter>
#include <QTimer>
#include <QEnterEvent>
#include <QCursor>

int main(int argc,char **argv) {
    QApplication app(argc,argv);
    QWidget desktop; desktop.resize(720,600); QPalette backdrop=desktop.palette(); backdrop.setColor(QPalette::Window,QColor("#171722")); desktop.setPalette(backdrop); desktop.setAutoFillBackground(true); desktop.show();
    const QString config=qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!config.contains("menu-preview")) return 10;
    QDir().mkpath(config);
    auto theme=[&](bool dark) {
        QSettings s(config+"/kdeglobals",QSettings::IniFormat);
        s.setValue("Colors:Window/BackgroundNormal",dark ? "31,31,37":"248,247,252");
        s.setValue("Colors:Window/ForegroundNormal",dark ? "228,225,233":"30,28,37");
        s.setValue("Colors:Selection/BackgroundNormal","194,193,255");
        s.setValue("Colors:Selection/ForegroundNormal","35,34,77"); s.sync();
    };
    theme(true);
    InlineMenu menu(&desktop),sub(&desktop),sort(&desktop);
    bool subClicked=false;
    sort.addAction("Name",[]{});
    sub.addAction("Folder",[&]{subClicked=true;});sub.addAction("Document",[]{});sub.addAction("Ярлык приложения…",[]{});
    menu.addSub("New",&sub); menu.addAction("Open in Terminal",[]{});menu.addSep();
    menu.addSub("Arrange Icons By",&sort);menu.addCheckable("Auto Arrange Icons",true,[]{});
    menu.addCheckable("Show Hidden Files",false,[]{});menu.addSep();
    bool clicked=false;menu.addAction("Refresh",[&]{clicked=true;});menu.addSep();
    menu.addAction("System",[]{});menu.addAction("Properties…",[]{});
    menu.popup(QPoint(70,50));QTest::qWait(220);
    if(menu.palette().window().color()!=QColor(31,31,37)) return 11;
    QTest::mouseMove(&menu,menu.itemRect(0).center());QTest::qWait(220);
    if(!sub.isVisible()) return 12;
    desktop.grab().save(qEnvironmentVariable("OMD_PREVIEW_OUT")+"/menu-dark.png");
    // Change file with the menu still open: verify automatic theme refresh.
    theme(false);QTest::qWait(250);
    if(menu.palette().window().color()!=QColor(248,247,252)) return 13;
    desktop.grab().save(qEnvironmentVariable("OMD_PREVIEW_OUT")+"/menu-light.png");
    QTest::mouseClick(&menu,Qt::LeftButton,Qt::NoModifier,menu.itemRect(7).center());
    if(!clicked) return 14;
    menu.popup(QPoint(719,599));QTest::qWait(220);
    if(!desktop.rect().contains(menu.geometry())) return 15;
    auto open = [&] {
        menu.closeAllImmediate();
        menu.popup(QPoint(70,50));
        QTest::mouseMove(&menu,menu.itemRect(0).center());
        QTest::qWait(200);
    };
    auto enterChild = [&](InlineMenu &child) {
        const QPoint local=child.itemRect(0).center();
        const QPoint global=child.mapToGlobal(local);
        QCursor::setPos(global);
        QEnterEvent event(local, local, global);
        QApplication::sendEvent(&child,&event);
        QTest::mouseMove(&child,local);
    };
    // Regression: the last seven pixels of the parent used to close the child.
    open();
    QTest::mouseMove(&menu,QPoint(menu.width()-3,menu.itemRect(0).center().y()));
    QTest::qWait(400);
    if(!sub.isVisible()) return 20;
    enterChild(sub); QTest::qWait(400);
    if(!sub.isVisible()) return 21;
    QTest::mouseClick(&sub,Qt::LeftButton,Qt::NoModifier,sub.itemRect(0).center());
    QTest::qWait(200);
    if(!subClicked || menu.isVisible() || sub.isVisible())return 22;
    // Moving diagonally briefly crosses another parent row.
    open();
    QTest::mouseMove(&menu,menu.itemRect(1).center());QTest::qWait(100);
    enterChild(sub);QTest::qWait(450);
    if(!sub.isVisible())return 23;
    // Switching to a different submenu closes the previous one.
    QTest::mouseMove(&menu,menu.itemRect(3).center());QTest::qWait(200);
    if(sub.isVisible() || !sort.isVisible())return 24;
    enterChild(sort);
    QCursor::setPos(desktop.mapToGlobal(QPoint(5,5)));
    QEvent leave(QEvent::Leave);QApplication::sendEvent(&sort,&leave);
    QTest::qWait(350);
    if(sort.isVisible())return 25;
    // The same crossing works when the submenu opens left near the screen edge.
    menu.closeAllImmediate();menu.popup(QPoint(719,599));
    QTest::mouseMove(&menu,menu.itemRect(0).center());QTest::qWait(200);
    if(sub.x() >= menu.x() || !desktop.rect().contains(sub.geometry()))return 26;
    QTest::mouseMove(&menu,QPoint(2,menu.itemRect(0).center().y()));
    enterChild(sub);QTest::qWait(400);
    if(!sub.isVisible())return 27;
    // Closing the root cancels outstanding submenu timers.
    QTest::mouseMove(&menu,menu.itemRect(1).center());
    menu.closeAllImmediate();QTest::qWait(400);
    if(menu.isVisible() || sub.isVisible() || sort.isVisible())return 28;
    puts("Theme, submenu crossing, diagonal entry, left edge, switching, close: PASS");
    return 0;
}
