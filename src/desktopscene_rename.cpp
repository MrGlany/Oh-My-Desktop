#include "desktopscene.h"
#include "desktopicon.h"
#include "desktoptextdialog.h"
#include "desktoplayout.h"
#include "applicationicons.h"
#include "filetransfer.h"
#include "themeutil.h"
#include <QPlainTextEdit>
#include <QFocusEvent>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>
#include <sys/stat.h>

static QByteArray renameIdentity(const QString &path){
    struct stat info{};
    if(::lstat(QFile::encodeName(path).constData(),&info)!=0)return {};
    return QByteArray::number(qulonglong(info.st_dev))+':'+QByteArray::number(qulonglong(info.st_ino))+':'+
        QByteArray::number(qulonglong(info.st_mode))+':'+QByteArray::number(qlonglong(info.st_size))+':'+
        QByteArray::number(qlonglong(info.st_mtim.tv_sec))+':'+QByteArray::number(qlonglong(info.st_mtim.tv_nsec))+':'+
        QByteArray::number(qlonglong(info.st_ctim.tv_sec))+':'+QByteArray::number(qlonglong(info.st_ctim.tv_nsec));
}

bool DesktopScene::renameEntry(const QFileInfo &file,const QString &name,const QByteArray &identity,QString *error){
    const QString oldName=file.fileName(),oldPath=file.absoluteFilePath();
    QString target=name.trimmed();
    if(target.isEmpty()||target=="."||target==".."||target.contains('/')||target.contains(QChar::Null)||target.contains('\n')||target.contains('\r')){
        *error="Укажите одно непустое имя без пути к файлу.";return false;
    }
    if(file.suffix().compare("desktop",Qt::CaseInsensitive)==0&&!target.endsWith(".desktop",Qt::CaseInsensitive))target+=".desktop";
    if(identity.isEmpty()||renameIdentity(oldPath)!=identity){
        *error="Объект изменился или был удалён. Отмените переименование и выберите его заново.";return false;
    }
    if(target==oldName)return true;
    const QString newPath=file.absolutePath()+"/"+target;
    if(!FileTransfer::run(oldPath,newPath,true,false,this,error)){
        *error="Не удалось переименовать. Существующие файлы не заменены.\n"+*error;return false;
    }
    ApplicationIcons::instance()->moveOverride(oldPath,newPath);
    claimEntry(target);m_selectAfterRename=target;
    if(m_positions.contains(oldName)){
        m_positions[target]=m_positions.take(oldName);queueSavePositions(kSaveDebounceMs);
    }
    queueRefresh(50);return true;
}

void DesktopScene::onRenameRequested(DesktopIcon *icon){
    finishInlineRename(false);
    if(!icon||icon->isVirtual())return;
    const QFileInfo file=icon->fileInfo();
    const QByteArray identity=renameIdentity(file.absoluteFilePath());
    QString seed=file.fileName();
    if(file.suffix().compare("desktop",Qt::CaseInsensitive)==0){
        QFile input(file.absoluteFilePath());
        if(input.open(QIODevice::ReadOnly|QIODevice::Text)){
            QTextStream stream(&input);bool main=false;
            while(!stream.atEnd()){
                const QString line=stream.readLine().trimmed();
                if(line.startsWith('[')){main=line=="[Desktop Entry]";continue;}
                if(main&&line.startsWith("Name=")&&!line.mid(5).trimmed().isEmpty()){seed=line.mid(5).trimmed();break;}
            }
        }
    }
    bool ok=false;
    const bool wasWatching=m_watcher->blockSignals(true);
    const QString name=DesktopDialogs::getText(this,m_screen,"Переименование","Новое имя:",seed,&ok);
    m_watcher->blockSignals(wasWatching);queueRefresh(50);
    if(!ok||name.trimmed().isEmpty())return;
    QString error;
    if(!renameEntry(file,name,identity,&error)){
        DesktopDialogs::DesktopKeyboardPause pause;
        QMessageBox warning(QMessageBox::Warning,"Переименование",error,QMessageBox::Ok,nullptr);
        warning.setWindowModality(Qt::ApplicationModal);warning.exec();
    }
}

void DesktopScene::onInlineRenameRequested(DesktopIcon *icon){
    if(!icon||icon->isVirtual()||!m_icons.contains(icon)||!icon->isSelected()||!icon->isVisible())return;
    int count=0;for(auto *item:m_icons)if(item->isSelected())++count;
    if(count!=1||m_nativeDragging||m_pageTransition)return;
    for(auto *menu:{m_ctxMenu,m_iconCtxMenu,m_ctxArrangeMenu,m_newMenu,m_systemMenu})if(menu&&menu->isVisible())return;
    finishInlineRename(false);closeAllMenus();
    m_tooltipTimer->stop();m_tooltipWindow->hide();
    m_inlineFile=icon->fileInfo();m_inlineIdentity=renameIdentity(m_inlineFile.absoluteFilePath());
    if(m_inlineIdentity.isEmpty())return;
    auto *box=new QWidget(this);m_inlineBox=box;box->setObjectName("InlineRenamePanel");
    box->setPalette(desktopThemePalette());
    box->setAutoFillBackground(true);box->setBackgroundRole(QPalette::Base);
    box->setStyleSheet("QWidget#InlineRenamePanel { border: 1px solid palette(highlight); border-radius: 4px; }");
    auto *layout=new QVBoxLayout(box);layout->setContentsMargins(3,3,3,3);layout->setSpacing(3);
    auto *edit=new QPlainTextEdit(box);m_inlineEditor=edit;edit->setObjectName("InlineRenameEditor");
    edit->setAccessibleName("Новое имя");edit->setFont(icon->labelFont());edit->setFrameShape(QFrame::NoFrame);
    edit->setTabChangesFocus(true);edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    edit->setWordWrapMode(QTextOption::WrapAnywhere);
    edit->setPlainText(m_inlineFile.fileName());
    edit->setToolTip("Enter — сохранить, Esc — отменить");layout->addWidget(edit);
    auto *error=new QLabel(box);m_inlineError=error;error->setObjectName("InlineRenameError");error->setWordWrap(true);error->hide();layout->addWidget(error);
    const QRect area=rect().adjusted(3,3,-3,-3);
    box->setFixedWidth(qMin(area.width(),qMax(140,icon->width()+64)));
    auto fitEditor=[this,edit,box]{
        const QFontMetrics metrics(edit->font());
        const QRect bounds=metrics.boundingRect(QRect(0,0,qMax(40,box->width()-16),10000),Qt::TextWrapAnywhere,edit->toPlainText());
        edit->setFixedHeight(qBound(metrics.lineSpacing()+10,bounds.height()+10,metrics.lineSpacing()*4+10));
        box->adjustSize();
        box->move(box->x(),qMax(3,qMin(box->y(),height()-box->height()-3)));
    };
    fitEditor();
    connect(edit,&QPlainTextEdit::textChanged,box,[this,fitEditor]{if(m_inlineError)m_inlineError->hide();fitEditor();});
    const int top=icon->y()+kIconLabelMarginTop+m_iconSize+kIconLabelMarginTop;
    box->move(qBound(area.left(),icon->geometry().center().x()-box->width()/2,area.right()-box->width()+1),
              qBound(area.top(),top,area.bottom()-box->height()+1));
    edit->installEventFilter(this);
    box->show();box->raise();edit->setFocus(Qt::MouseFocusReason);
    const QString name=m_inlineFile.fileName();
    const int dot=!m_inlineFile.isDir()?name.lastIndexOf('.'):-1;
    QTextCursor cursor=edit->textCursor();cursor.setPosition(0);cursor.setPosition(dot>0?dot:name.size(),QTextCursor::KeepAnchor);edit->setTextCursor(cursor);
}

void DesktopScene::finishInlineRename(bool accept){
    if(!m_inlineEditor)return;
    if(accept&&!DesktopLayout::instance()->visible(m_inlineFile.fileName(),m_screenName))accept=false;
    if(accept){
        QString error;
        if(!renameEntry(m_inlineFile,m_inlineEditor->toPlainText(),m_inlineIdentity,&error)){
            m_inlineError->setText(error);m_inlineError->show();m_inlineBox->adjustSize();
            m_inlineBox->move(m_inlineBox->x(),qMax(3,qMin(m_inlineBox->y(),height()-m_inlineBox->height()-3)));
            m_inlineEditor->setFocus(Qt::OtherFocusReason);return;
        }
    }
    auto *box=m_inlineBox.data();
    m_inlineEditor->removeEventFilter(this);
    m_inlineEditor=nullptr;m_inlineBox=nullptr;m_inlineError=nullptr;m_inlineIdentity.clear();
    if(box){box->hide();box->deleteLater();}
    setFocus(Qt::OtherFocusReason);queueRefresh(50);
}

bool DesktopScene::filterInlineRenameEvent(QObject *object,QEvent *event){
    if(!m_inlineEditor||object!=m_inlineEditor)return false;
    if(event->type()==QEvent::KeyPress){
        auto *key=static_cast<QKeyEvent*>(event);
        if(key->key()==Qt::Key_Escape){finishInlineRename(false);return true;}
        if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){finishInlineRename(true);return true;}
    }
    if(event->type()==QEvent::FocusOut){
        const bool accept=static_cast<QFocusEvent*>(event)->reason()!=Qt::ActiveWindowFocusReason;
        QPointer<QPlainTextEdit> edit=m_inlineEditor;
        QTimer::singleShot(0,this,[this,edit,accept]{if(edit&&edit==m_inlineEditor&&!edit->hasFocus())finishInlineRename(accept);});
    }
    return false;
}
