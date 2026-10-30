#include "dolphinintegration.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QAction>
#include <QStandardPaths>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KService>

int main(int argc,char **argv){
    QApplication app(argc,argv);
    if(!qEnvironmentVariable("XDG_DATA_HOME").contains("dolphin-check"))return 10;
    const QString menu=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/kio/servicemenus/oh-my-desktop-link.desktop";
    QDir().mkpath(QFileInfo(menu).absolutePath());
    // Refuse to overwrite or remove an unrelated file, even at our usual path.
    QFile unrelated(menu);if(!unrelated.open(QIODevice::WriteOnly))return 11;
    unrelated.write("[Desktop Entry]\nName=User-owned\n");unrelated.close();
    {DolphinIntegration guard;if(guard.activate()||DolphinIntegration::cleanupStopped())return 12;}
    if(!unrelated.open(QIODevice::ReadOnly)||unrelated.readAll()!="[Desktop Entry]\nName=User-owned\n")return 13;
    unrelated.close();unrelated.remove();
    QTemporaryDir fixtures;QDir().mkdir(fixtures.path()+"/Folder");
    KFileItemList items;items<<KFileItem(QUrl::fromLocalFile(fixtures.path()+"/Folder"));
    KFileItemActions builder;builder.setItemListProperties(KFileItemListProperties(items));
    auto visible=[&]{bool found=false;for(auto *a:builder.createServiceMenuActions())if(a->text()==QString::fromUtf8("Создать ярлык на рабочем столе"))found=true;return found;};
    if(visible())return 14;
    {
        DolphinIntegration integration;if(!integration.activate())return 15;
        KService service(menu);if(!service.isValid()||service.actions().size()!=1)return 16;
        if(!visible())return 17;
        if(DolphinIntegration::cleanupStopped()||!QFile::exists(menu))return 18;
        integration.deactivate();if(visible()||QFile::exists(menu))return 19;
        if(!integration.activate()||!visible())return 20;
    }
    if(visible()||QFile::exists(menu))return 21;
    {
        DolphinIntegration integration;if(!integration.activate())return 22;
        QFile file(menu);if(!file.open(QIODevice::ReadOnly))return 23;
        QByteArray content=file.readAll();file.close();
        content.replace(QByteArray("X-OhMyDesktop-Pid=")+QByteArray::number(QCoreApplication::applicationPid()),"X-OhMyDesktop-Pid=2147483647");
        if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return 24;file.write(content);file.close();
        if(!DolphinIntegration::cleanupStopped()||visible())return 25;
    }
    puts("PASS same KDE menu builder: absent -> running -> stopped -> running -> destructor; crash cleanup, live owner protection and unrelated file preservation");
    return 0;
}
