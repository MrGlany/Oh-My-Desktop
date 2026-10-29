#include "launcherutil.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--open")) return LauncherUtil::openLocation("trash:/") ? 0 : 1;
    const QString data=qEnvironmentVariable("XDG_DATA_HOME");
    if(!data.contains("trash-check")) return 10;
    QDir().mkpath(data);
    QTemporaryDir test(data+"/fixture-XXXXXX");
    const QString source=test.path()+"/тест.txt";
    QFile f(source); if(!f.open(QIODevice::WriteOnly)) return 11;
    f.write("safe trash check");f.close();
    QString trashed;
    if(!QFile::moveToTrash(source,&trashed)) return 12;
    QFile saved(trashed);
    if(QFile::exists(source) || !saved.open(QIODevice::ReadOnly) || saved.readAll()!="safe trash check") return 13;
    saved.close();
    if(!QFile::rename(trashed,source)) return 14;
    const QString link=test.path()+"/ярлык.txt";
    if(!QFile::link(source,link))return 15;
    if(!QFile::moveToTrash(link,&trashed))return 16;
    if(!QFileInfo(trashed).isSymLink() || !QFile::exists(source))return 17;
    QFile original(source);if(!original.open(QIODevice::ReadOnly) || original.readAll()!="safe trash check")return 18;
    const QString folder=test.path()+"/folder",folderLink=test.path()+"/folder-link";
    if(!QDir().mkdir(folder) || !QFile::link(folder,folderLink))return 19;
    if(!QFile::moveToTrash(folderLink,&trashed))return 20;
    if(!QFileInfo(trashed).isSymLink() || !QFileInfo(folder).isDir())return 21;
    puts("Trash file/folder links leave originals intact: PASS");
    return 0;
}
