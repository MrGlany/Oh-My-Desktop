#pragma once
#include <QObject>
#include <QStringList>
#include <QFileSystemWatcher>
#include <QTimer>

class TrashMonitor : public QObject {
    Q_OBJECT
public:
    explicit TrashMonitor(const QStringList &locations = {}, QObject *parent = nullptr);
    static TrashMonitor *instance();
    bool full() const { return m_full; }
signals:
    void changed(bool full);
private:
    void refresh();
    QStringList m_locations;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    bool m_full = false;
};
