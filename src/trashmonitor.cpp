#include "trashmonitor.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QPointer>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <unistd.h>

TrashMonitor *TrashMonitor::instance() {
    static QPointer<TrashMonitor> monitor;
    if (!monitor) monitor = new TrashMonitor({}, QCoreApplication::instance());
    return monitor;
}

TrashMonitor::TrashMonitor(const QStringList &locations, QObject *parent)
    : QObject(parent), m_locations(locations) {
    if (m_locations.isEmpty()) {
        m_locations << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/Trash/files";
        const QString uid=QString::number(getuid());
        for (const auto &storage : QStorageInfo::mountedVolumes()) {
            const auto type=storage.fileSystemType();
            if (!storage.isReady() || storage.isReadOnly() ||
                !(type=="btrfs" || type=="ext4" || type=="xfs" || type=="ntfs" ||
                  type=="ntfs3" || type=="fuseblk" || type=="exfat" || type=="vfat")) continue;
            const QDir root(storage.rootPath());
            m_locations << root.filePath(".Trash-"+uid+"/files") << root.filePath(".Trash/"+uid+"/files");
        }
        m_locations.removeDuplicates();
    }
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_watcher,&QFileSystemWatcher::directoryChanged,this,[this]{m_debounce.start();});
    connect(&m_debounce,&QTimer::timeout,this,&TrashMonitor::refresh);
    refresh();
}

void TrashMonitor::refresh() {
    bool full=false;
    QSet<QString> wanted;
    for (const QString &path:m_locations) {
        if (QFileInfo(path).isDir() && !QDir(path).isEmpty(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot)) full=true;
        QString ancestor=path;
        while (!QFileInfo(ancestor).isDir()) {
            const QString parent=QFileInfo(ancestor).absolutePath();
            if (parent==ancestor) break;
            ancestor=parent;
        }
        // Inaccessible system mount points cannot contain this user's trash.
        if (access(QFile::encodeName(ancestor).constData(), R_OK | X_OK) == 0)
            wanted.insert(ancestor);
        const QString parent = QFileInfo(path).absolutePath();
        if (QFileInfo(path).isDir() && access(QFile::encodeName(parent).constData(), R_OK | X_OK) == 0)
            wanted.insert(parent);
    }
    const auto watched=m_watcher.directories();
    for (const QString &path:watched) if(!wanted.contains(path))m_watcher.removePath(path);
    for (const QString &path:wanted) if(!watched.contains(path))m_watcher.addPath(path);
    if (full!=m_full) {m_full=full;emit changed(full);}
}
