#include "pagecontrol.h"
#include "desktopscene.h"
#include "desktoplayout.h"
#include <QLocalSocket>
#include <QStandardPaths>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

PageControl::PageControl(QObject *parent):QObject(parent) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server,&QLocalServer::newConnection,this,[this]{
        while(m_server.hasPendingConnections()) {
            auto *socket=m_server.nextPendingConnection();socket->setParent(this);socket->setReadBufferSize(4097);
            connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
            QTimer::singleShot(3000,socket,[socket]{socket->disconnectFromServer();});
            connect(socket,&QLocalSocket::readyRead,this,[socket]{
                if(socket->property("handled").toBool())return;
                if(socket->bytesAvailable()>4096){socket->abort();return;}
                if(!socket->canReadLine())return;
                socket->setProperty("handled",true);
                const auto request=QJsonDocument::fromJson(socket->readLine()).object();
                QJsonObject response{{"ok",false}};
                const QString command=request.value("command").toString();
                for(auto *widget:QApplication::allWidgets()) {
                    auto *scene=qobject_cast<DesktopScene*>(widget);
                    if(!scene||scene->monitorName()!=request.value("monitor").toString())continue;
                    if(command=="step") {
                        const int direction=request.value("direction").toInt();
                        if(direction==1||direction==-1)response["ok"]=scene->stepPage(direction);
                    } else if(command=="activate")response["ok"]=scene->setCurrentPage(request.value("page").toString());
                    else if(command=="status")response["ok"]=true;
                    auto *layout=DesktopLayout::instance();QJsonArray pages;
                    for(const auto &p:layout->pages(scene->monitorName()))pages.append(QJsonObject{{"id",p.id},{"name",p.name}});
                    response["pages"]=pages;response["active"]=layout->activePage(scene->monitorName());
                    break;
                }
                socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact)+'\n');socket->disconnectFromServer();
            });
        }
    });
}
bool PageControl::listen() {
    const QString path=qEnvironmentVariable("OMD_CONTROL_SOCKET",QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/oh-my-desktop-pages.sock");
    m_lock=std::make_unique<QLockFile>(path+".lock");m_lock->setStaleLockTime(0);
    if(!m_lock->tryLock(0)){qWarning("Another desktop instance owns the page control socket");return false;}
    QLocalServer::removeServer(path);
    if(!m_server.listen(path)){qWarning("Cannot listen on desktop page control socket");return false;}
    return true;
}
PageControl::~PageControl(){m_server.close();}
