#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>
class PageControl:public QObject {
    Q_OBJECT
public:
    explicit PageControl(QObject *parent=nullptr);
    ~PageControl() override;
    bool listen();
private:
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lock;
};
