#pragma once
#include "desktoplayout.h"
#include <QWidget>
#include <QVariantAnimation>

class PagePager : public QWidget {
    Q_OBJECT
public:
    explicit PagePager(QWidget *parent=nullptr);
    void setPages(const QList<DesktopPage> &pages,const QString &active,const QString &monitor);
signals:
    void activated(const QString &id);
    void stepped(int direction);
    void settingsRequested();
    void pageDrop(const QString &id,QObject *source,const QStringList &keys);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
private:
    int indexAt(QPoint position) const;
    int firstVisible() const;
    QList<DesktopPage> m_pages;
    QString m_active,m_monitor;
    int m_hover=-1;
};
