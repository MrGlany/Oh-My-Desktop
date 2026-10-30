#pragma once
#include <QObject>
#include <QIcon>
#include <QFileInfo>
#include <QHash>
#include <QQueue>
#include <QSet>

class ApplicationIcons : public QObject {
    Q_OBJECT
public:
    static ApplicationIcons *instance();
    QIcon icon(const QFileInfo &file,bool ignoreOverride=false);
    QString overridePath(const QFileInfo &file)const;
    bool setOverride(const QFileInfo &file,const QString &image);
    void moveOverride(const QString &oldPath,const QString &newPath);
signals:
    void changed(const QString &path);
private:
    explicit ApplicationIcons(QObject *parent=nullptr):QObject(parent){}
    struct Entry{QString stamp;QIcon icon;qint64 checked=0;};
    QHash<QString,Entry> m_cache;
    QQueue<QString> m_queue;
    QSet<QString> m_pending;
    int m_running=0;
    void startNext();
};
