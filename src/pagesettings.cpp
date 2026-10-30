#include "pagesettings.h"
#include "pagecarousel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUuid>
#include <QGuiApplication>
#include <QScreen>

PageSettings::PageSettings(QWidget *parent):QWidget(parent),m_draft(DesktopLayout::instance()->pageSets()) {
    setObjectName("DesktopPagesSettings");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(8);
    auto *monitorRow=new QHBoxLayout;
    monitorRow->addWidget(new QLabel("Монитор",this));
    m_monitor=new QComboBox(this);m_monitor->setObjectName("PageMonitor");
    for(const auto &name:m_draft.keys()) {
        QString label=name;bool connected=false;
        for(auto *screen:QGuiApplication::screens())if(DesktopLayout::screenName(screen)==name)connected=true;
        if(!connected)label+=" · отключён";
        else if(name==DesktopLayout::instance()->primaryScreen())label+=" · основной";
        m_monitor->addItem(label,name);
    }
    monitorRow->addWidget(m_monitor,1);layout->addLayout(monitorRow);
    m_carousel=new PageCarousel(this);layout->addWidget(m_carousel,1);
    auto *nameRow=new QHBoxLayout;
    auto *nameLabel=new QLabel("Название",this);nameRow->addWidget(nameLabel);
    m_name=new QLineEdit(this);m_name->setObjectName("PageName");m_name->setMaxLength(64);
    m_name->setPlaceholderText("Название выбранной страницы");nameLabel->setBuddy(m_name);
    nameRow->addWidget(m_name,1);layout->addLayout(nameRow);
    auto *actions=new QHBoxLayout;
    actions->addWidget(new QLabel("Порядок",this));
    m_left=new QPushButton("←",this);m_left->setObjectName("PageMoveLeft");m_left->setFixedWidth(46);
    m_right=new QPushButton("→",this);m_right->setObjectName("PageMoveRight");m_right->setFixedWidth(46);
    m_left->setToolTip("Поменять выбранную страницу местами с левой");m_right->setToolTip("Поменять выбранную страницу местами с правой");
    m_left->setAccessibleName("Передвинуть страницу влево");m_right->setAccessibleName("Передвинуть страницу вправо");
    actions->addWidget(m_left);actions->addWidget(m_right);actions->addStretch();
    m_remove=new QPushButton("Удалить страницу",this);m_remove->setObjectName("PageRemove");
    actions->addWidget(m_remove);layout->addLayout(actions);
    auto *hint=new QLabel("Нажмите соседний экран или листайте колесом. «+» по краям добавляет страницу. Изменения — после «Применить» или «ОК».",this);
    hint->setWordWrap(true);hint->setObjectName("PageCarouselHint");layout->addWidget(hint);
    connect(m_monitor,&QComboBox::currentIndexChanged,this,[this]{reload();});
    connect(m_carousel,&PageCarousel::currentChanged,this,&PageSettings::updateSelection);
    connect(m_carousel,&PageCarousel::addRequested,this,&PageSettings::add);
    connect(m_name,&QLineEdit::textEdited,this,[this](const QString &text){
        const int row=m_carousel->currentIndex();auto &pages=m_draft[monitor()];
        if(row<0||row>=pages.size())return;
        pages[row].name=text;
        QSignalBlocker blocked(m_carousel);
        m_carousel->setPages(pages,pages[row].id,DesktopLayout::instance()->activePage(monitor()));
    });
    connect(m_left,&QPushButton::clicked,this,[this]{move(-1);});connect(m_right,&QPushButton::clicked,this,[this]{move(1);});
    connect(m_remove,&QPushButton::clicked,this,[this]{
        const int row=m_carousel->currentIndex();auto &pages=m_draft[monitor()];
        if(row<0||row>=pages.size()||pages[row].id=="main")return;
        if(QMessageBox::question(this,"Удалить страницу?","Значки вернутся на основную страницу этого монитора. Файлы останутся на месте.")!=QMessageBox::Yes)return;
        pages.removeAt(row);reload();
    });
    reload();
}
QString PageSettings::monitor() const{return m_monitor->currentData().toString();}
void PageSettings::selectMonitor(const QString &name){int i=m_monitor->findData(name);if(i>=0)m_monitor->setCurrentIndex(i);}
void PageSettings::updateSelection(int row){
    const auto pages=m_draft.value(monitor());const bool valid=row>=0&&row<pages.size();
    QSignalBlocker blocked(m_name);m_name->setEnabled(valid);m_name->setText(valid?pages[row].name:QString());
    m_remove->setEnabled(valid&&pages[row].id!="main");m_remove->setToolTip(valid&&pages[row].id=="main"?"Основную страницу удалить нельзя":"Значки вернутся на основную; файлы не удаляются");
    m_left->setEnabled(valid&&row>0);m_right->setEnabled(valid&&row+1<pages.size());
}
void PageSettings::reload(const QString &selected){
    const auto pages=m_draft.value(monitor());
    m_carousel->setPages(pages,selected.isEmpty()?"main":selected,DesktopLayout::instance()->activePage(monitor()));
}
void PageSettings::add(int direction){
    auto &pages=m_draft[monitor()];if(pages.size()>=64){QMessageBox::information(this,"Страницы","На одном мониторе поддерживается до 64 страниц.");return;}
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    int row=qMax(0,m_carousel->currentIndex());pages.insert(qMin(row+(direction>0?1:0),int(pages.size())),{id,"Страница "+QString::number(pages.size()+1)});reload(id);
}
void PageSettings::move(int direction){
    auto &pages=m_draft[monitor()];int row=m_carousel->currentIndex(),next=row+direction;
    if(row<0||next<0||next>=pages.size())return;
    const QString id=pages[row].id;pages.move(row,next);reload(id);
}
bool PageSettings::commit(){
    const auto live=DesktopLayout::instance()->pageSets();
    for(auto it=live.cbegin();it!=live.cend();++it)if(!m_draft.contains(it.key()))m_draft.insert(it.key(),it.value());
    if(DesktopLayout::instance()->replacePages(m_draft))return true;
    QMessageBox::warning(this,"Страницы","Не удалось сохранить страницы. Проверьте названия и доступ к настройкам.");return false;
}
