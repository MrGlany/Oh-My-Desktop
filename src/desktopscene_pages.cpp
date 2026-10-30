#include "desktopscene.h"
#include "desktoplayout.h"
#include "desktopicon.h"
#include "pagepager.h"
#include <QPainter>
#include <QVariantAnimation>
#include <QResizeEvent>
#include <QTimer>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QDir>
#include <QFileInfo>

using PageImage=QList<QPair<QPoint,QPixmap>>;
static PageImage capturePage(const QList<DesktopIcon*> &icons) {
    PageImage image;
    for(auto *icon:icons)if(!icon->isHidden())image.append({icon->pos(),icon->grab()});
    return image;
}
class PageTransition : public QWidget {
public:
    PageImage before,after; qreal progress=0;int direction=1;
    explicit PageTransition(QWidget *parent):QWidget(parent) {
        setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(parent->rect());
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(),Qt::transparent);p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setOpacity(1-progress);
        for(const auto &item:before)p.drawPixmap(item.first+QPoint(qRound(-80*direction*progress),0),item.second);
        p.setOpacity(progress);
        for(const auto &item:after)p.drawPixmap(item.first+QPoint(qRound(80*direction*(1-progress)),0),item.second);
    }
};
void DesktopScene::initializePages() {
    m_pager=new PagePager(this);
    connect(m_pager,&PagePager::activated,this,[this](const QString &id){setCurrentPage(id);});
    connect(m_pager,&PagePager::stepped,this,[this](int direction){stepPage(direction);});
    connect(m_pager,&PagePager::settingsRequested,this,&DesktopScene::onShowProperties);
    connect(m_pager,&PagePager::pageDrop,this,[this](const QString &id,QObject *source,const QStringList &keys){
        if(moveIconsToPage(qobject_cast<DesktopScene*>(source),keys,id))setCurrentPage(id);
    });
    updatePager();
}
void DesktopScene::updatePager() {
    if(!m_pager)return;
    auto *layout=DesktopLayout::instance();
    m_pager->setPages(layout->pages(m_screenName),layout->activePage(m_screenName),m_screenName);
    const QRect area=menuBoundary();
    m_pager->move(area.center().x()-m_pager->width()/2,area.bottom()-m_pager->height()-11);
    m_pager->show();m_pager->raise();
}
bool DesktopScene::cellReserved(int col,int row) const {
    return m_pager && QRect(cellToPos(col,row),QSize(m_iconSize+kIconExtraWidth,m_iconSize+kIconExtraHeight)).intersects(m_pager->geometry().adjusted(-6,-4,6,4));
}
void DesktopScene::resizeEvent(QResizeEvent *e) {
    finishPageAnimation();QWidget::resizeEvent(e);updatePager();
}
bool DesktopScene::setCurrentPage(const QString &id) {
    m_pageStepDirection=0;
    if(m_nativeDragging)return false;
    return DesktopLayout::instance()->activatePage(m_screenName,id);
}
bool DesktopScene::stepPage(int direction) {
    auto *layout=DesktopLayout::instance();const auto pages=layout->pages(m_screenName);
    if(!direction||pages.isEmpty())return false;
    if(pages.size()==1)return true;
    int index=0;for(int n=0;n<pages.size();++n)if(pages[n].id==layout->activePage(m_screenName))index=n;
    const int step=direction>0?1:-1;
    const int next=(index+step+int(pages.size()))%int(pages.size());
    const bool changed=setCurrentPage(pages[next].id);
    if(changed)m_pageStepDirection=step;
    return changed;
}
bool DesktopScene::moveIconsToPage(DesktopScene *source,const QStringList &keys,const QString &page) {
    if(!source||source->m_desktopPath!=m_desktopPath||keys.isEmpty())return false;
    QSet<QString> unique;
    for(const auto &key:keys) {
        if(unique.contains(key))return false;unique.insert(key);
        bool found=false;for(auto *icon:source->m_icons)if(source->iconKey(icon)==key)found=true;
        if(!found)return false;
    }
    source->savePositions();
    if(!DesktopLayout::instance()->assign(keys,m_screenName,page))return false;
    source->queueRefresh(0);queueRefresh(0);return true;
}
bool DesktopScene::placeDesktopLinks(const QStringList &names,const QString &page){
    if(names.isEmpty()||names.size()>64)return false;
    QSet<QString> seen;
    for(const auto &name:names){
        if(name.isEmpty()||name=="."||name==".."||name.contains('/')||name.contains('\\')||name.contains(QChar(0))||name.startsWith("__")||name.startsWith(".omd-")||seen.contains(name))return false;
        seen.insert(name);
        if(!QFileInfo(QDir(m_desktopPath).filePath(name)).isSymLink())return false;
    }
    if(!DesktopLayout::instance()->assign(names,m_screenName,page))return false;
    queueRefresh(0);return true;
}
void DesktopScene::chooseIconPage() {
    QStringList keys;
    for(auto *icon:m_icons)if(icon->isSelected())keys.append(iconKey(icon));
    if(keys.isEmpty())return;
    struct Target{QPointer<DesktopScene> scene;QString page;};
    QList<Target> targets;QStringList labels;
    for(auto *widget:QApplication::allWidgets())if(auto *scene=qobject_cast<DesktopScene*>(widget)) {
        if(scene->m_desktopPath!=m_desktopPath)continue;
        for(const auto &page:DesktopLayout::instance()->pages(scene->m_screenName)) {
            if(scene==this&&page.id==m_pageId)continue;
            targets.append({scene,page.id});labels.append(scene->m_screenName+" · "+page.name);
        }
    }
    if(targets.isEmpty()){QMessageBox::information(this,"Страницы","Сначала добавьте страницу в свойствах рабочего стола.");return;}
    bool ok=false;const QString choice=QInputDialog::getItem(this,"Переместить значки","Монитор и страница:",labels,0,false,&ok);
    int i=labels.indexOf(choice);if(ok&&i>=0&&targets[i].scene)targets[i].scene->moveIconsToPage(this,keys,targets[i].page);
}
void DesktopScene::finishPageAnimation() {
    if(m_pageAnimation){m_pageAnimation->stop();m_pageAnimation->deleteLater();m_pageAnimation=nullptr;}
    if(m_pageTransition){m_pageTransition->hide();m_pageTransition->deleteLater();m_pageTransition=nullptr;
        for(auto *icon:m_icons)icon->show();update();}
}
void DesktopScene::syncCurrentPage() {
    if(m_nativeDragging)return;
    auto *layout=DesktopLayout::instance();const QString next=layout->activePage(m_screenName);
    if(next==m_pageId){m_pageStepDirection=0;updatePager();queueRefresh(0);return;}
    const int stepDirection=m_pageStepDirection;m_pageStepDirection=0;
    finishPageAnimation();closeAllMenus();savePositions();
    const auto before=capturePage(m_icons);
    int oldIndex=0,newIndex=0;const auto pages=layout->pages(m_screenName);
    for(int n=0;n<pages.size();++n){if(pages[n].id==m_pageId)oldIndex=n;if(pages[n].id==next)newIndex=n;}
    for(auto *icon:m_icons){icon->hide();icon->deleteLater();}
    m_icons.clear();m_occupiedCells.clear();m_dragStartPositions.clear();
    m_pageId=next;m_positions.clear();loadPositions();refresh();updatePager();
    if(qEnvironmentVariableIsSet("OMD_NO_ANIMATIONS")||!isVisible())return;
    auto *transition=new PageTransition(this);transition->before=before;transition->after=capturePage(m_icons);
    transition->direction=stepDirection?stepDirection:(newIndex>=oldIndex?1:-1);
    for(auto *icon:m_icons)icon->hide();
    m_pageTransition=transition;transition->show();transition->raise();m_pager->raise();
    auto *animation=new QVariantAnimation(this);m_pageAnimation=animation;
    animation->setStartValue(0.0);animation->setEndValue(1.0);animation->setDuration(240);animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation,&QVariantAnimation::valueChanged,transition,[transition](const QVariant &v){transition->progress=v.toReal();transition->update();});
    connect(animation,&QVariantAnimation::finished,this,&DesktopScene::finishPageAnimation);animation->start();
}
