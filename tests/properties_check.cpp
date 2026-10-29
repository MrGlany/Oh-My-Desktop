#include "propertiesdialog.h"
#include "desktopscene.h"
#include "desktopicon.h"
#include "trashmonitor.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QListWidget>
#include <QLineEdit>
#include <QTabWidget>
#include <QPainter>
#include <QUuid>
#include <QApplication>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QTest>
#include <QFile>
#include <QSettings>
#include <QDir>
#include <QTranslator>
#include <QLibraryInfo>
#include <QLabel>
#include <QIcon>
#include <QCursor>
#include <QStandardPaths>
#include <qpa/qplatformscreen.h>
#include <qpa/qwindowsysteminterface.h>

class RemovedTestScreen : public QPlatformScreen {
public:
    QRect geometry() const override { return QRect(0,0,1920,1080); }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_ARGB32_Premultiplied; }
    QString name() const override { return QStringLiteral("REMOVAL-TEST"); }
};

static QCheckBox *check(PropertiesDialog *d,const QString &text) {
    for(auto *c:d->findChildren<QCheckBox*>()) if(c->text()==text)return c;
    return nullptr;
}
static PropertiesDialog *dialog() {
    return qobject_cast<PropertiesDialog*>(QApplication::activeModalWidget());
}
static bool computerIcon(DesktopScene &scene) {
    for(auto *icon:scene.findChildren<DesktopIcon*>())
        if(icon->virtualType()==DesktopIcon::VirtualType::Computer && !icon->isHidden())return true;
    return false;
}
#include "monitor_check.h"
int main(int argc,char **argv) {
    QApplication app(argc,argv);
    if(!qEnvironmentVariable("XDG_CONFIG_HOME").contains("properties-check"))return 10;
    if (app.arguments().contains("--icons-preview")) {
        QIcon::setThemeSearchPaths({"/usr/share/icons"});
        QIcon::setFallbackSearchPaths({"/usr/share/pixmaps"});
        QIcon::setThemeName("breeze");
        QWidget canvas; canvas.resize(500,450);
        QPalette colors=canvas.palette();colors.setColor(QPalette::Window,QColor("#202027"));
        canvas.setPalette(colors);canvas.setAutoFillBackground(true);
        int y=15;
        const QStringList files{"/home/coder/Рабочий стол/org.telegram.desktop.desktop", "/home/coder/Рабочий стол/com.microsoft.VSCode.desktop"};
        for(int size : {40,64,96}) {
            auto *label=new QLabel(QString::number(size)+" px",&canvas);label->move(10,y);label->setStyleSheet("color:white;");
            int x=100;
            for(const auto &file:files) {
                auto *icon=new DesktopIcon(QFileInfo(file),size,false,&canvas);
                icon->setLabelFont("Adwaita Mono",8);icon->move(x,y);x+=170;
            }
            y+=size+65;
        }
        canvas.show();QCursor::setPos(canvas.mapToGlobal(QPoint(490,440)));QTest::qWait(100);
        const QPixmap image=canvas.grab();
        if(image.width()!=qRound(canvas.width()*canvas.devicePixelRatioF()))return 20;
        if(!image.save(qEnvironmentVariable("OMD_ICON_PREVIEW")))return 21;
        for(const QString &name : {QString("org.telegram.desktop"),QString("vscode")}) {
            const QIcon icon=QIcon::fromTheme(name);
            const auto small=icon.pixmap(40,40);const auto large=icon.pixmap(80,80);
            printf("%s: 40px request -> %dx%d; 80px request -> %dx%d\n",qPrintable(name),small.width(),small.height(),large.width(),large.height());
        }
        return 0;
    }
    QTranslator ru;if(ru.load("qtbase_ru",QLibraryInfo::path(QLibraryInfo::TranslationsPath)))app.installTranslator(&ru);
    app.setApplicationName("PropertiesTest");app.setOrganizationName("PropertiesTest");
    if(app.arguments().contains("--monitor-check"))return monitorCheck(false);
    if(app.arguments().contains("--monitor-check-reload"))return monitorCheck(true);
    {
        auto *platform = new RemovedTestScreen;
        QWindowSystemInterface::handleScreenAdded(platform);
        QPointer<QScreen> screen = platform->screen();
        const QString config = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        const QString saved = config+"/icon-positions-REMOVAL-TEST.ini";
        {
            QSettings seed(saved, QSettings::IniFormat);
            seed.setValue("positions/screen-removal-probe", QPoint(321,123));
            seed.sync();
        }
        auto *scene = new DesktopScene(screen);
        QFile::remove(saved);
        QWindowSystemInterface::handleScreenRemoved(platform);
        // handleScreenRemoved owns deletion of both the platform and public screen.
        if (screen) return 90;
        delete scene; // Must save to the original screen's file after QScreen destruction.
        QSettings result(saved, QSettings::IniFormat);
        if (result.value("positions/screen-removal-probe").toPoint()!=QPoint(321,123)) return 91;
        qInfo("PASS screen removal preserves monitor identity and safely saves positions");
    }
    DesktopScene left(app.primaryScreen()),right(app.primaryScreen());
    left.resize(1920,1080);right.resize(1920,1080);
    left.show();right.show();QTest::qWait(150);
    bool ok=true;
    // Isolated trash: missing -> created -> full -> emptied -> removed/recreated.
    QTemporaryDir trashFixture(qEnvironmentVariable("XDG_CONFIG_HOME")+"/trash-XXXXXX");
    const QString trashFiles=trashFixture.path()+"/Trash/files";
    TrashMonitor monitor({trashFiles});QSignalSpy changes(&monitor,&TrashMonitor::changed);
    ok &= !monitor.full();
    QDir().mkpath(trashFiles);QTest::qWait(180);
    QFile item(trashFiles+"/test");ok &= item.open(QIODevice::WriteOnly);item.write("test");item.close();
    QTest::qWait(180);ok &= monitor.full();
    ok &= item.remove();QTest::qWait(180);ok &= !monitor.full();
    QDir().rmdir(trashFiles);QTest::qWait(180);QDir().mkpath(trashFiles);
    ok &= item.open(QIODevice::WriteOnly);item.close();QTest::qWait(180);ok &= monitor.full();
    ok &= item.remove();QTest::qWait(180);ok &= !monitor.full() && changes.count()>=4;

    // App picker uses only a generated .desktop fixture, never executes it.
    const QString runId=QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString appName="Проверка ярлыка "+runId;
    const QString appDir=qEnvironmentVariable("XDG_DATA_HOME")+"/applications";
    if(!appDir.contains("properties-check"))return 30;
    QDir().mkpath(appDir);
    const QString source=appDir+"/fixture-"+runId+".desktop";
    QFile launcher(source);ok &= launcher.open(QIODevice::WriteOnly);
    const QByteArray data=("[Desktop Entry]\nType=Application\nName="+appName+"\nExec=/bin/true\nIcon=application-x-executable\n").toUtf8();
    launcher.write(data);launcher.close();
    for(int copy=0;copy<2;++copy) {
        QTimer::singleShot(100,[&,copy]{
            auto *d=dialog();if(!d){ok=false;return;}
            ok &= d->findChild<QTabWidget*>()->count()==1;
            for(auto *edit:d->findChildren<QLineEdit*>())
                if(edit->placeholderText()=="Поиск приложений")edit->setText(appName);
            auto *list=d->findChild<QListWidget*>();QListWidgetItem *match=nullptr;
            for(int i=0;i<list->count();++i)if(list->item(i)->text()==appName)match=list->item(i);
            if(!match){ok=false;d->reject();return;}
            list->setCurrentItem(match);
            if(copy==0)d->grab().save(qEnvironmentVariable("OMD_PREVIEW_OUT")+"/application-picker.png");
            for(auto *button:d->findChildren<QPushButton*>())
                if(button->text()=="Создать ярлык"){button->click();break;}
        });
        QMetaObject::invokeMethod(&left,"onApplicationShortcut",Qt::DirectConnection);
        QFile result(qEnvironmentVariable("OMD_DESKTOP_DIR")+"/"+appName+(copy?" (2)":"")+".desktop");
        ok &= result.open(QIODevice::ReadOnly) && result.readAll()==data;
    }

    // Render into a reused transparent surface after many size changes. It must
    // match a clean frame, with exactly one live Trash widget on each desktop.
    for(auto *scene:{&left,&right}) {
        QImage previous(scene->size(),QImage::Format_ARGB32_Premultiplied);previous.fill(Qt::transparent);
        scene->render(&previous,QPoint(),QRegion(),QWidget::DrawChildren);
        for(int size:{24,96,40,72,32,64,40}) {
            auto settings=scene->desktopSettings();settings.iconSize=size;
            scene->applyDesktopSettings(settings,false);QTest::qWait(10);
            scene->render(&previous,QPoint(),QRegion(),QWidget::DrawChildren);
            QImage fresh(scene->size(),QImage::Format_ARGB32_Premultiplied);fresh.fill(Qt::transparent);
            scene->render(&fresh,QPoint(),QRegion(),QWidget::DrawChildren);
            ok &= fresh==previous;
            int count=0;
            for(auto *icon:scene->findChildren<DesktopIcon*>(QString(),Qt::FindDirectChildrenOnly))
                if(icon->virtualType()==DesktopIcon::VirtualType::Trash && !icon->isHidden())++count;
            ok &= count==1;
        }
    }
    puts(ok?"Trash state, picker/collision names, transparent resize frames: PASS":"Integration regression: FAIL");

    QTimer::singleShot(100,[&]{
        auto *d=dialog();if(!d){ok=false;return;}
        check(d,"Этот компьютер")->setChecked(true);
        auto *buttons=d->findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Apply)->click();
        ok &= left.desktopSettings().showComputer && right.desktopSettings().showComputer;
        ok &= computerIcon(left) && computerIcon(right);
        d->grab().save(qEnvironmentVariable("OMD_PREVIEW_OUT")+"/properties-dark.png");
        QSettings theme(qEnvironmentVariable("XDG_CONFIG_HOME")+"/kdeglobals",QSettings::IniFormat);
        const auto previous=theme.value("Colors:Window/BackgroundNormal");
        theme.setValue("Colors:Window/BackgroundNormal",QStringList{"248","247","252"});theme.sync();
        QTest::qWait(100);
        ok &= d->palette().window().color()==QColor(248,247,252);
        theme.setValue("Colors:Window/BackgroundNormal",previous);theme.sync();
        QTest::qWait(100);
        // Cancel after Apply must preserve the committed state.
        check(d,"Домашняя папка")->setChecked(true);
        d->findChild<QSlider*>()->setValue(72);
        d->reject();
    });
    QMetaObject::invokeMethod(&left,"onShowProperties",Qt::DirectConnection);
    ok &= left.desktopSettings().showComputer && !left.desktopSettings().showHome;
    ok &= left.desktopSettings().iconSize==40;
    QTimer::singleShot(100,[&]{
        auto *d=dialog();auto *buttons=d->findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::RestoreDefaults)->click();
        ok &= !d->currentSettings().showComputer && d->currentSettings().showTrash;
        buttons->button(QDialogButtonBox::Apply)->click();
        ok &= !left.desktopSettings().showComputer && !right.desktopSettings().showComputer;
        d->reject();
    });
    QMetaObject::invokeMethod(&left,"onShowProperties",Qt::DirectConnection);
    QTest::qWait(50);
    // Reload persisted preferences in a fresh desktop.
    DesktopScene reloaded(app.primaryScreen());
    ok &= !reloaded.desktopSettings().showComputer && reloaded.desktopSettings().showTrash;
    // A translated launcher name must not break .desktop parsing.
    puts(ok?"Properties Apply/Cancel/Reset/persistence: PASS":"FAIL");
    return ok?0:1;
}
