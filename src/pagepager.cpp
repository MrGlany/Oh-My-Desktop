#include "pagepager.h"
#include "wheelutil.h"
#include "themeutil.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileSystemWatcher>
#include <QFileInfo>

PagePager::PagePager(QWidget *parent):QWidget(parent) {
    setObjectName("DesktopPagePager");setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setAcceptDrops(true);
    setPalette(desktopThemePalette());setAccessibleName("Страницы рабочего стола");
    auto *watcher=new QFileSystemWatcher(this);
    const QString file=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+"/kdeglobals";
    watcher->addPath(QFileInfo(file).absolutePath());if(QFileInfo::exists(file))watcher->addPath(file);
    auto theme=[this,watcher,file]{setPalette(desktopThemePalette());update();if(QFileInfo::exists(file)&&!watcher->files().contains(file))watcher->addPath(file);};
    connect(watcher,&QFileSystemWatcher::fileChanged,this,theme);
    connect(watcher,&QFileSystemWatcher::directoryChanged,this,theme);
}
int PagePager::firstVisible() const {
    int current=0;for(int n=0;n<m_pages.size();++n)if(m_pages[n].id==m_active)current=n;
    return qBound(0,current-4,qMax(0,int(m_pages.size())-9));
}
void PagePager::setPages(const QList<DesktopPage> &pages,const QString &active,const QString &monitor) {
    m_pages=pages;m_active=active;m_monitor=monitor;
    setFixedSize(72+24*qMin(9,int(pages.size())),40);
    QString name;for(const auto &p:pages)if(p.id==active)name=p.name;
    setToolTip(monitor+" · "+name+"\nКолесо: страницы · ПКМ: настройки\nПеретащите значок на точку нужной страницы");
    update();
}
int PagePager::indexAt(QPoint p) const {
    if(!rect().contains(p))return -1;
    if(p.x()<32)return -2;
    if(p.x()>=width()-32)return -3;
    int n=(p.x()-36)/24+firstVisible();return n>=0&&n<m_pages.size()?n:-1;
}
void PagePager::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Source);p.fillRect(rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QColor bg=palette().window().color();bg.setAlpha(235);
    QColor border=palette().windowText().color();border.setAlpha(35);
    p.setPen(QPen(border,1));p.setBrush(bg);p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),20,20);
    p.setPen(QPen(palette().windowText().color(),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.drawPolyline(QPolygonF{QPointF(20,16),QPointF(16,20),QPointF(20,24)});
    p.drawPolyline(QPolygonF{QPointF(width()-20,16),QPointF(width()-16,20),QPointF(width()-20,24)});
    for(int n=firstVisible();n<qMin(firstVisible()+9,int(m_pages.size()));++n) {
        const qreal x=48+(n-firstVisible())*24;
        const bool active=m_pages[n].id==m_active;
        QColor color=active?palette().highlight().color():palette().windowText().color();
        if(!active)color.setAlpha(n==m_hover?210:110);
        p.setPen(Qt::NoPen);p.setBrush(color);
        if(active)p.drawRoundedRect(QRectF(x-11,14,22,12),6,6);
        else if(m_pages[n].id=="main") {p.setPen(QPen(color,1.6));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(x,20),4,4);}
        else p.drawEllipse(QPointF(x,20),4,4);
    }
}
void PagePager::mousePressEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton){QWidget::mousePressEvent(e);return;}
    const int n=indexAt(e->position().toPoint());
    if(n==-2)emit stepped(-1);else if(n==-3)emit stepped(1);else if(n>=0)emit activated(m_pages[n].id);
    e->accept();
}
void PagePager::mouseMoveEvent(QMouseEvent *e) {m_hover=indexAt(e->position().toPoint());update();}
void PagePager::wheelEvent(QWheelEvent *e) {const int step=desktopWheelStep(e);if(step)emit stepped(step);e->accept();}
void PagePager::keyPressEvent(QKeyEvent *e) {
    if(e->key()==Qt::Key_Left)emit stepped(-1);else if(e->key()==Qt::Key_Right)emit stepped(1);else {QWidget::keyPressEvent(e);return;}e->accept();
}
void PagePager::contextMenuEvent(QContextMenuEvent *e) {emit settingsRequested();e->accept();}
void PagePager::dragEnterEvent(QDragEnterEvent *e) {if(e->source()&&e->mimeData()->hasFormat("application/x-oh-my-desktop-icons")){e->setDropAction(Qt::MoveAction);e->accept();}}
void PagePager::dragMoveEvent(QDragMoveEvent *e) {if(indexAt(e->position().toPoint())>=0&&e->source()&&e->mimeData()->hasFormat("application/x-oh-my-desktop-icons")){e->setDropAction(Qt::MoveAction);e->accept();}else e->ignore();}
void PagePager::dropEvent(QDropEvent *e) {
    int n=indexAt(e->position().toPoint());if(n<0||!e->source()){e->ignore();return;}
    QStringList keys;for(const auto &v:QJsonDocument::fromJson(e->mimeData()->data("application/x-oh-my-desktop-icons")).array())if(v.isString())keys.append(v.toString());
    if(keys.isEmpty()){e->ignore();return;}
    emit pageDrop(m_pages[n].id,e->source(),keys);e->setDropAction(Qt::MoveAction);e->accept();
}
