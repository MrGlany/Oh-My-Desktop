#include "pagecarousel.h"
#include "wheelutil.h"
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

namespace {
class AddPageButton : public QPushButton {
public:
    explicit AddPageButton(const QString &label,QWidget *parent):QPushButton(label,parent) {
        setCursor(Qt::PointingHandCursor);setAccessibleName(label);setToolTip(label);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        const auto colors=parentWidget()->palette();const QColor accent=colors.highlight().color();
        QColor fill=accent;fill.setAlpha(underMouse()?32:13);
        QColor edge=accent;edge.setAlpha(underMouse()||hasFocus()?220:90);
        p.setBrush(fill);p.setPen(QPen(edge,1.4,Qt::DashLine));
        p.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1),15,15);
        const QPointF center(width()/2.,height()/2.-16);
        QColor circle=accent;circle.setAlpha(underMouse()?50:25);p.setPen(Qt::NoPen);p.setBrush(circle);p.drawEllipse(center,18,18);
        p.setPen(QPen(accent,2,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(center+QPointF(-6,0),center+QPointF(6,0));p.drawLine(center+QPointF(0,-6),center+QPointF(0,6));
        QFont f=font();f.setPointSizeF(qMax(8.,f.pointSizeF()-.5));p.setFont(f);p.setPen(colors.windowText().color());
        const int textTop=qRound(center.y()+22);
        p.drawText(QRect(5,textTop,width()-10,height()-textTop-6),Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap,text());
    }
};
}

PageCarousel::PageCarousel(QWidget *parent):QWidget(parent) {
    setObjectName("PageCarousel");setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
    setMinimumHeight(242);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setAccessibleName("Карусель страниц рабочего стола");
    setToolTip("Нажмите соседний экран или листайте колесом. Плюс по краям добавляет страницу.");
    m_addLeft=new AddPageButton("Добавить\nслева",this);m_addLeft->setObjectName("PageAddLeft");
    m_addRight=new AddPageButton("Добавить\nсправа",this);m_addRight->setObjectName("PageAddRight");
    m_previous=new QPushButton("‹",this);m_previous->setObjectName("PagePrevious");m_previous->setToolTip("Предыдущая страница");
    m_next=new QPushButton("›",this);m_next->setObjectName("PageNext");m_next->setToolTip("Следующая страница");
    m_home=new QPushButton("К основной",this);m_home->setObjectName("PageHome");m_home->setToolTip("Показать основную страницу в редакторе");
    for(auto *button:{m_previous,m_next,m_home})button->setCursor(Qt::PointingHandCursor);
    connect(m_addLeft,&QPushButton::clicked,this,[this]{emit addRequested(-1);});
    connect(m_addRight,&QPushButton::clicked,this,[this]{emit addRequested(1);});
    connect(m_previous,&QPushButton::clicked,this,[this]{setCurrentIndex(m_current-1);});
    connect(m_next,&QPushButton::clicked,this,[this]{setCurrentIndex(m_current+1);});
    connect(m_home,&QPushButton::clicked,this,[this]{for(int i=0;i<m_pages.size();++i)if(m_pages[i].id=="main"){setCurrentIndex(i);return;}});
    m_slide.setDuration(230);m_slide.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_slide,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){m_visual=v.toReal();positionControls();update();});
}
QString PageCarousel::selectedPage() const {return m_current>=0&&m_current<m_pages.size()?m_pages[m_current].id:QString();}
void PageCarousel::setPages(const QList<DesktopPage> &pages,const QString &selected,const QString &active) {
    m_slide.stop();m_pages=pages;m_active=active;m_current=0;
    for(int n=0;n<pages.size();++n)if(pages[n].id==selected)m_current=n;
    m_visual=m_current;
    positionControls();update();emit currentChanged(m_current);
}
void PageCarousel::setCurrentIndex(int index) {
    if(index<0||index>=m_pages.size()||index==m_current)return;
    m_slide.stop();m_current=index;
    emit currentChanged(index);
    if(qEnvironmentVariableIsSet("OMD_NO_ANIMATIONS")){m_visual=index;positionControls();update();return;}
    m_slide.setStartValue(m_visual);m_slide.setEndValue(qreal(index));m_slide.start();
    positionControls();update();
}
QRectF PageCarousel::screenRect(int index) const {
    const qreal distance=index-m_visual;
    const qreal scale=1-.22*qMin(qAbs(distance),qreal(1));
    const qreal mainWidth=qMin(qreal(212),width()*.427);
    const qreal mainHeight=mainWidth*124/212;
    const qreal w=mainWidth*scale,h=mainHeight*scale;
    const qreal spacing=qMin(qreal(178),width()*.36);
    const qreal top=qMax(qreal(18),(height()-238)/2.);
    return QRectF(width()/2.+distance*spacing-w/2.,top+(mainHeight-h)/2.,w,h);
}
void PageCarousel::positionControls() {
    if(m_pages.isEmpty())return;
    auto addRect=[this](int index){const QRectF r=screenRect(index);const qreal w=qMin(qreal(128),width()*.26);return QRectF(r.center().x()-w/2,r.center().y()-54,w,108).toAlignedRect();};
    m_addLeft->setGeometry(addRect(-1));m_addRight->setGeometry(addRect(m_pages.size()));
    m_addLeft->setVisible(m_current==0);m_addRight->setVisible(m_current==m_pages.size()-1);
    m_addLeft->setEnabled(m_pages.size()<64);m_addRight->setEnabled(m_pages.size()<64);
    const int y=height()-34;
    m_previous->setGeometry(10,y,34,30);m_next->setGeometry(width()-44,y,34,30);
    m_previous->setEnabled(m_current>0);m_next->setEnabled(m_current+1<m_pages.size());
    m_home->setGeometry(width()/2.-62,y,124,30);
    m_home->setEnabled(selectedPage()!="main");m_home->setText(selectedPage()=="main"?"Основная":"К основной");
}
void PageCarousel::drawScreen(QPainter &p,int index) {
    const auto &page=m_pages[index];const QRectF screen=screenRect(index);
    const qreal distance=qAbs(index-m_visual);const bool selected=index==m_current;
    const auto colors=palette();QColor accent=colors.highlight().color();
    p.save();p.setOpacity(qMax(.24,1-distance*.38));
    QColor shadow(Qt::black);shadow.setAlpha(selected?40:20);
    p.setBrush(shadow);p.setPen(Qt::NoPen);p.drawRoundedRect(screen.translated(0,5),14,14);
    QLinearGradient fill(screen.topLeft(),screen.bottomRight());
    fill.setColorAt(0,colors.base().color());QColor tinted=colors.window().color();
    fill.setColorAt(1,tinted);p.setBrush(fill);
    QColor border=selected?accent:colors.mid().color();if(!selected)border.setAlpha(80);
    p.setPen(QPen(border,selected?2:1));p.drawRoundedRect(screen,14,14);
    const QRectF view=screen.adjusted(7,7,-7,-16);
    QPainterPath clip;clip.addRoundedRect(view,9,9);p.save();p.setClipPath(clip);
    QColor glow=accent;glow.setAlpha(selected?32:15);p.fillRect(view,glow);
    glow.setAlpha(selected?34:15);p.setBrush(glow);p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(view.right()-15,view.top()+10),60,60);
    p.drawEllipse(QPointF(view.left()+10,view.bottom()+10),60,45);
    // Abstract monitor illustration; no wallpaper capture or file thumbnails.
    QColor tile=colors.windowText().color();tile.setAlpha(80);p.setBrush(tile);
    for(int n=0;n<3;++n)p.drawRoundedRect(QRectF(view.left()+10,view.top()+10+n*14,7,7),2,2);
    p.setPen(QPen(accent,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);
    const QPointF c=view.center();
    if(page.id=="main") {
        p.drawPolyline(QPolygonF{c+QPointF(-12,-2),c+QPointF(0,-12),c+QPointF(12,-2)});
        p.drawRoundedRect(QRectF(c.x()-8,c.y()-2,16,14),2,2);p.drawLine(c+QPointF(0,12),c+QPointF(0,5));
    } else {
        QFont f=font();f.setPointSizeF(17);f.setWeight(QFont::DemiBold);p.setFont(f);p.setPen(accent);
        p.drawText(view,Qt::AlignCenter,QString::number(index+1));
    }
    p.restore();
    p.setBrush(border);p.setPen(Qt::NoPen);p.drawRoundedRect(QRectF(screen.center().x()-4,screen.bottom()+1,8,6),2,2);
    p.drawRoundedRect(QRectF(screen.center().x()-19,screen.bottom()+6,38,3),1.5,1.5);
    QFont f=font();f.setWeight(selected?QFont::DemiBold:QFont::Normal);p.setFont(f);p.setPen(colors.windowText().color());
    QRectF title(screen.left()-4,screen.bottom()+19,screen.width()+8,22);
    title=title.intersected(QRectF(4,0,width()-8,height()));
    p.drawText(title,Qt::AlignCenter,p.fontMetrics().elidedText(page.name,Qt::ElideRight,title.width()));
    f.setWeight(QFont::Normal);f.setPointSizeF(qMax(8.,f.pointSizeF()-1));p.setFont(f);
    int mainIndex=0;for(int n=0;n<m_pages.size();++n)if(m_pages[n].id=="main")mainIndex=n;
    QString subtitle=page.id==m_active?"Сейчас открыта":(page.id=="main"?"Основная страница":(index<mainIndex?"Слева от основной":"Справа от основной"));
    QColor sub=colors.windowText().color();sub.setAlpha(135);p.setPen(sub);
    p.drawText(title.translated(0,21),Qt::AlignCenter,p.fontMetrics().elidedText(subtitle,Qt::ElideRight,title.width()));
    p.restore();
}
void PageCarousel::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::TextAntialiasing);
    p.setClipRect(QRect(0,0,width(),height()-38));
    QList<int> visible;for(int n=qMax(0,int(std::floor(m_visual))-2);n<qMin(int(m_pages.size()),int(std::ceil(m_visual))+3);++n)visible.append(n);
    std::sort(visible.begin(),visible.end(),[this](int a,int b){return qAbs(a-m_visual)>qAbs(b-m_visual);});
    for(int n:visible)drawScreen(p,n);
}
void PageCarousel::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){m_press=e->position().toPoint();m_pressed=true;e->accept();}else QWidget::mousePressEvent(e);}
void PageCarousel::mouseReleaseEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton||!m_pressed){QWidget::mouseReleaseEvent(e);return;}
    m_pressed=false;const int dx=e->position().toPoint().x()-m_press.x();
    if(qAbs(dx)>36){setCurrentIndex(m_current+(dx<0?1:-1));e->accept();return;}
    QList<int> candidates;for(int n=0;n<m_pages.size();++n)if(screenRect(n).contains(e->position()))candidates.append(n);
    if(!candidates.isEmpty()){std::sort(candidates.begin(),candidates.end(),[this](int a,int b){return qAbs(a-m_visual)<qAbs(b-m_visual);});setCurrentIndex(candidates.first());}
    e->accept();
}
void PageCarousel::mouseMoveEvent(QMouseEvent *e){setCursor(m_pressed?Qt::ClosedHandCursor:Qt::PointingHandCursor);QWidget::mouseMoveEvent(e);}
void PageCarousel::wheelEvent(QWheelEvent *e){const int step=desktopWheelStep(e);if(step)setCurrentIndex(m_current+step);e->accept();}
void PageCarousel::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Left)setCurrentIndex(m_current-1);else if(e->key()==Qt::Key_Right)setCurrentIndex(m_current+1);else if(e->key()==Qt::Key_Home){for(int n=0;n<m_pages.size();++n)if(m_pages[n].id=="main")setCurrentIndex(n);}else{QWidget::keyPressEvent(e);return;}e->accept();}
void PageCarousel::resizeEvent(QResizeEvent *e){QWidget::resizeEvent(e);positionControls();}
