#include "filepropertiesdialog.h"
#include "desktopconstants.h"
#include "applicationicons.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QSaveFile>
#include <QImageReader>
#include <QFileIconProvider>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <sys/stat.h>

static QString fileIdentity(const QString &path){struct stat s{};if(::lstat(QFile::encodeName(path).constData(),&s)!=0)return {};return QString::number(s.st_dev)+":"+QString::number(s.st_ino);}

static QString humanSize(qint64 bytes)
{
    const QLocale locale = QLocale::system();
    if (bytes < 1024LL)
        return QString("%1 bytes").arg(locale.toString(bytes));
    if (bytes < 1024LL * 1024)
        return QString("%1 KB (%2 bytes)")
            .arg(locale.toString(bytes / 1024.0, 'f', 1))
            .arg(locale.toString(bytes));
    if (bytes < 1024LL * 1024 * 1024)
        return QString("%1 MB (%2 bytes)")
            .arg(locale.toString(bytes / (1024.0 * 1024), 'f', 1))
            .arg(locale.toString(bytes));
    return QString("%1 GB (%2 bytes)")
        .arg(locale.toString(bytes / (1024.0 * 1024 * 1024), 'f', 2))
        .arg(locale.toString(bytes));
}

FilePropertiesDialog::FilePropertiesDialog(const QFileInfo &info, QWidget *parent)
    : QDialog(parent, Qt::Dialog)
    , m_info(info)
    , m_isDesktopEntry(info.suffix().compare("desktop", Qt::CaseInsensitive) == 0)
{
    m_fileIdentity=fileIdentity(info.absoluteFilePath());
    setWindowTitle("Свойства");
    setObjectName("PropertiesDialog");
    setFixedWidth(kPropertiesDialogWidth);
    setSizeGripEnabled(false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *banner = new QFrame(this);
    banner->setObjectName("PropBanner");
    banner->setFixedHeight(kPropertiesDialogBannerHeight);
    {
        auto *bl = new QHBoxLayout(banner);
        bl->setContentsMargins(10, 6, 10, 6);
        bl->setSpacing(10);

        auto *iconLbl = new QLabel(banner);

        m_iconPreview=iconLbl;m_iconPreview->setObjectName("DesktopIconPreview");
        m_customIcon=m_originalCustomIcon=ApplicationIcons::instance()->overridePath(m_info);
        connect(ApplicationIcons::instance(),&ApplicationIcons::changed,this,[this](const QString &path){if(path==m_info.absoluteFilePath())updateIconPreview();});
        updateIconPreview();

        auto *nameLbl = new QLabel(info.fileName(), banner);
        nameLbl->setObjectName("PropBannerText");
        nameLbl->setWordWrap(true);

        bl->addWidget(iconLbl);
        bl->addWidget(nameLbl, 1);
    }
    root->addWidget(banner);

    auto *content = new QWidget(this);
    content->setObjectName("PropContent");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(kPropertiesDialogContentMargins, 6, kPropertiesDialogContentMargins, 6);
    contentLayout->setSpacing(6);

    QMimeDatabase mimeDb;
    QString typeStr;
    if (info.isDir()) {
        typeStr = "Папка";
    } else {
        const auto mime = mimeDb.mimeTypeForFile(info);
        typeStr = mime.comment().isEmpty() ? mime.name() : mime.comment();
    }

    auto *infoGroup = new QGroupBox("Общие", content);
    auto *form = new QFormLayout(infoGroup);
    form->setContentsMargins(kPropertiesDialogContentMargins, 6, kPropertiesDialogContentMargins, 6);
    form->setSpacing(3);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto addRow = [&](const QString &label, const QString &value) {
        auto *v = new QLabel(value, infoGroup);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        form->addRow(label, v);
    };

    addRow("Тип:",     typeStr);
    addRow("Расположение:", info.absolutePath());
    if (!info.isDir())
        addRow("Размер:", humanSize(info.size()));
    addRow("Создан:",  QLocale::system().toString(info.birthTime(),     QLocale::ShortFormat));
    addRow("Изменён:", QLocale::system().toString(info.lastModified(),  QLocale::ShortFormat));
    contentLayout->addWidget(infoGroup);

    if (m_isDesktopEntry) {
        auto *launcherGroup = new QGroupBox("Ярлык приложения", content);
        auto *launcherForm = new QFormLayout(launcherGroup);
        launcherForm->setContentsMargins(8, 6, 8, 6);
        launcherForm->setSpacing(4);
        launcherForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_nameEdit = new QLineEdit(launcherGroup);
        m_execEdit = new QLineEdit(launcherGroup);
        m_iconEdit = new QLineEdit(launcherGroup);

        launcherForm->addRow("Название:", m_nameEdit);
        launcherForm->addRow("Команда:", m_execEdit);
        launcherForm->addRow("Значок:", m_iconEdit);
        contentLayout->addWidget(launcherGroup);

        loadDesktopEntryFields();
    }

    auto *iconGroup=new QGroupBox("Значок на рабочем столе",content);
    auto *iconRow=new QHBoxLayout(iconGroup);
    auto *choose=new QPushButton("Выбрать картинку…",iconGroup);choose->setObjectName("ChooseDesktopIcon");
    auto *automatic=new QPushButton("Автоматически",iconGroup);automatic->setObjectName("ResetDesktopIcon");
    iconRow->addWidget(choose);iconRow->addWidget(automatic);
    connect(choose,&QPushButton::clicked,this,[this]{
        const QString path=QFileDialog::getOpenFileName(this,"Значок рабочего стола",QString(),"Изображения (*.png *.svg *.ico *.webp *.jpg *.jpeg *.xpm)",nullptr,QFileDialog::DontUseNativeDialog);
        if(!path.isEmpty()){m_customIcon=path;updateIconPreview();}
    });
    connect(automatic,&QPushButton::clicked,this,[this]{m_customIcon.clear();updateIconPreview();});
    contentLayout->addWidget(iconGroup);

    auto *permGroup = new QGroupBox("Права доступа", content);
    {
        auto *pg = new QGridLayout(permGroup);
        pg->setContentsMargins(8, 6, 8, 6);
        pg->setSpacing(0);
        pg->setColumnMinimumWidth(0, 52);

        const char *headers[] = { "Чтение", "Запись", "Exec" };
        for (int c = 0; c < 3; ++c) {
            auto *h = new QLabel(headers[c], permGroup);
            h->setAlignment(Qt::AlignCenter);
            pg->addWidget(h, 0, c + 1);
        }

        struct PermRow { const char *label; QFile::Permission r, w, x; };
        const PermRow prows[] = {
            { "Владелец",  QFile::ReadOwner,  QFile::WriteOwner,  QFile::ExeOwner  },
            { "Группа",  QFile::ReadGroup,  QFile::WriteGroup,  QFile::ExeGroup  },
            { "Остальные", QFile::ReadOther,  QFile::WriteOther,  QFile::ExeOther  },
        };

        const QFile::Permissions perms = QFile::permissions(info.absoluteFilePath());
        for (int i = 0; i < 3; ++i) {
            pg->addWidget(new QLabel(prows[i].label, permGroup), i + 1, 0);
            const QFile::Permission cols[] = { prows[i].r, prows[i].w, prows[i].x };
            for (int j = 0; j < 3; ++j) {
                const bool on = bool(perms & cols[j]);
                auto *lbl = new QLabel(on ? "✓" : "–", permGroup);
                lbl->setAlignment(Qt::AlignCenter);
                lbl->setStyleSheet(on
                    ? "color: #00c800; font-weight: bold;"
                    : "color: #808080;");
                pg->addWidget(lbl, i + 1, j + 1);
            }
        }
    }
    contentLayout->addWidget(permGroup);

    auto *btnBox=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Apply|QDialogButtonBox::Cancel,content);
    connect(btnBox->button(QDialogButtonBox::Apply),&QPushButton::clicked,this,[this]{saveChanges();});
    connect(btnBox,&QDialogButtonBox::accepted,this,[this]{if(saveChanges())accept();});
    connect(btnBox,&QDialogButtonBox::rejected,this,&QDialog::reject);
    contentLayout->addWidget(btnBox);

    root->addWidget(content);
}

void FilePropertiesDialog::updateIconPreview()
{
    if(!m_iconPreview)return;
    QIcon icon;
    if(!m_customIcon.isEmpty()){
        QImageReader reader(m_customIcon);
        if(reader.canRead()&&reader.size().width()<=8192&&reader.size().height()<=8192)icon=QIcon(m_customIcon);
    }else icon=ApplicationIcons::instance()->icon(m_info,true);
    if(icon.isNull()){QFileIconProvider provider;icon=provider.icon(m_info);}
    m_iconPreview->setPixmap(icon.pixmap(32,32));
}
bool FilePropertiesDialog::saveChanges()
{
    if(m_fileIdentity.isEmpty()||fileIdentity(m_info.absoluteFilePath())!=m_fileIdentity){QMessageBox::warning(this,"Файл изменился","Файл был удалён или заменён. Откройте свойства снова.");return false;}
    if(!saveDesktopEntryFields())return false;
    if(m_customIcon!=m_originalCustomIcon){
        if(!ApplicationIcons::instance()->setOverride(QFileInfo(m_info.absoluteFilePath()),m_customIcon)){
            QMessageBox::warning(this,"Значок","Не удалось сохранить значок. Выберите доступную картинку.");return false;
        }
        m_originalCustomIcon=m_customIcon;
    }
    return true;
}

void FilePropertiesDialog::loadDesktopEntryFields()
{
    if (!m_isDesktopEntry || !m_nameEdit || !m_execEdit || !m_iconEdit)
        return;

    QFile file(m_info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    m_originalEntry=file.readAll();file.seek(0);
    QTextStream in(&file);
    bool inDesktopEntry = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith('[')) {
            inDesktopEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry || line.startsWith('#') || !line.contains('='))
            continue;

        const int split = line.indexOf('=');
        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();

        if (key == "Name") m_nameEdit->setText(value);
        else if (key == "Exec") m_execEdit->setText(value);
        else if (key == "Icon") m_iconEdit->setText(value);
    }
    m_originalFields={m_nameEdit->text(),m_execEdit->text(),m_iconEdit->text()};
}

bool FilePropertiesDialog::saveDesktopEntryFields()
{
    if (!m_isDesktopEntry || !m_nameEdit || !m_execEdit || !m_iconEdit)
        return true;

    if(QStringList{m_nameEdit->text(),m_execEdit->text(),m_iconEdit->text()}==m_originalFields)return true;
    if(QFileInfo(m_info.absoluteFilePath()).isSymLink()){
        QMessageBox::warning(this,"Ярлык приложения","Поля ссылки нельзя менять в исходном приложении. Для своего значка используйте «Выбрать картинку» ниже.");return false;
    }
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Ярлык приложения", "Название не может быть пустым.");
        return false;
    }

    QFile file(m_info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Ярлык приложения", "Не удалось открыть ярлык для редактирования.");
        return false;
    }
    if(file.readAll()!=m_originalEntry){QMessageBox::warning(this,"Ярлык изменился","Файл изменился. Закройте свойства и откройте их снова.");return false;}
    file.seek(0);
    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd())
        lines << in.readLine();
    file.close();

    bool inDesktopEntry = false;
    bool foundName = false;
    bool foundExec = false;
    bool foundIcon = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith('[')) {
            if (inDesktopEntry)
                break;
            inDesktopEntry = (trimmed == "[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry || trimmed.startsWith('#') || !trimmed.contains('='))
            continue;

        const int split = trimmed.indexOf('=');
        const QString key = trimmed.left(split).trimmed();
        if (key == "Name") {
            lines[i] = "Name=" + m_nameEdit->text().trimmed();
            foundName = true;
        } else if (key == "Exec") {
            lines[i] = "Exec=" + m_execEdit->text().trimmed();
            foundExec = true;
        } else if (key == "Icon") {
            lines[i] = "Icon=" + m_iconEdit->text().trimmed();
            foundIcon = true;
        }
    }

    int desktopEntryEnd = -1;
    inDesktopEntry = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed == "[Desktop Entry]") {
            inDesktopEntry = true;
            desktopEntryEnd = i + 1;
            continue;
        }
        if (inDesktopEntry && trimmed.startsWith('[')) {
            desktopEntryEnd = i;
            break;
        }
        if (inDesktopEntry)
            desktopEntryEnd = i + 1;
    }

    if (desktopEntryEnd < 0) {
        lines.prepend("[Desktop Entry]");
        desktopEntryEnd = 1;
    }
    if (!foundName) lines.insert(desktopEntryEnd++, "Name=" + m_nameEdit->text().trimmed());
    if (!foundExec) lines.insert(desktopEntryEnd++, "Exec=" + m_execEdit->text().trimmed());
    if (!foundIcon) lines.insert(desktopEntryEnd++, "Icon=" + m_iconEdit->text().trimmed());

    const QByteArray updated=(lines.join('\n')+'\n').toUtf8();
    QSaveFile output(m_info.absoluteFilePath());
    if(!output.open(QIODevice::WriteOnly)||!output.setPermissions(QFile::permissions(m_info.absoluteFilePath()))||output.write(updated)!=updated.size()||!output.commit()){
        QMessageBox::warning(this,"Ярлык приложения","Не удалось сохранить изменения. Исходный файл сохранён.");return false;
    }
    m_fileIdentity=fileIdentity(m_info.absoluteFilePath());
    m_originalEntry=updated;m_originalFields={m_nameEdit->text(),m_execEdit->text(),m_iconEdit->text()};

    return true;
}
