#include "propertiesdialog.h"
#include "desktopconstants.h"
#include "themeutil.h"
#include "pagesettings.h"
#include <QFileSystemWatcher>
#include <QScrollArea>
#include <QScreen>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {
struct DesktopApplicationEntry {
    QString name;
    QString icon;
    QString desktopFilePath;
};

DesktopApplicationEntry readDesktopApplication(const QString &filePath)
{
    DesktopApplicationEntry entry;
    entry.desktopFilePath = filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;

    QTextStream in(&file);
    bool inDesktopEntry = false;
    bool hidden = false;
    bool noDisplay = false;
    QString type = "Application";

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line.startsWith('[')) {
            inDesktopEntry = (line == "[Desktop Entry]");
            continue;
        }
        if (!inDesktopEntry || !line.contains('='))
            continue;

        const int split = line.indexOf('=');
        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();

        if (key == "Name" && entry.name.isEmpty()) {
            entry.name = value;
        } else if (key == "Icon" && entry.icon.isEmpty()) {
            entry.icon = value;
        } else if (key == "NoDisplay") {
            noDisplay = (value.compare("true", Qt::CaseInsensitive) == 0);
        } else if (key == "Hidden") {
            hidden = (value.compare("true", Qt::CaseInsensitive) == 0);
        } else if (key == "Type") {
            type = value;
        }
    }

    if (hidden || noDisplay || type.compare("Application", Qt::CaseInsensitive) != 0)
        return {};

    if (entry.name.isEmpty())
        entry.name = QFileInfo(filePath).completeBaseName();

    return entry;
}
}

PropertiesDialog::PropertiesDialog(const Settings &s, QWidget *parent, bool applicationsOnly)
    : QDialog(parent, Qt::Dialog)
{
    setWindowTitle(applicationsOnly ? "Создать ярлык приложения" : "Свойства рабочего стола");
    setObjectName("PropertiesDialog");
    setFixedWidth(560);
    setSizeGripEnabled(false);
    refreshTheme();
    auto *watcher=new QFileSystemWatcher(this);
    const QString config=QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    watcher->addPath(config); watcher->addPath(config+"/kdeglobals");
    connect(watcher,&QFileSystemWatcher::directoryChanged,this,[this]{refreshTheme();});
    connect(watcher,&QFileSystemWatcher::fileChanged,this,[this,watcher,config]{
        refreshTheme(); if(!watcher->files().contains(config+"/kdeglobals")) watcher->addPath(config+"/kdeglobals");
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 16);
    root->setSpacing(12);

    auto *banner = new QFrame(this);
    banner->setObjectName("PropBanner");
    banner->setFixedHeight(kPropertiesDialogBannerHeight);
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(10, 6, 10, 6);
    bannerLayout->setSpacing(10);

    auto *bannerIcon = new QLabel(banner);
    bannerIcon->setPixmap(QIcon::fromTheme("preferences-desktop",
        QIcon::fromTheme("preferences-system")).pixmap(32, 32));

    auto *bannerText = new QLabel(windowTitle(), banner);
    bannerText->setObjectName("PropBannerText");

    bannerLayout->addWidget(bannerIcon);
    bannerLayout->addWidget(bannerText);
    bannerLayout->addStretch();
    root->addWidget(banner);

    auto *content = new QWidget(this);
    content->setObjectName("PropContent");
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(kPropertiesDialogContentMargins, kPropertiesDialogContentMargins, kPropertiesDialogContentMargins, kPropertiesDialogContentMargins);
    contentLayout->setSpacing(8);

    auto *tabs = new QTabWidget(content);

    auto *generalTab = new QWidget(tabs);
    auto *generalLayout = new QVBoxLayout(generalTab);
    generalLayout->setContentsMargins(8, 8, 8, 8);
    generalLayout->setSpacing(8);

    auto *iconGroup = new QGroupBox("Значки", generalTab);
    auto *iconLayout = new QGridLayout(iconGroup);
    iconLayout->setColumnStretch(1, 1);
    iconLayout->setVerticalSpacing(8);

    iconLayout->addWidget(new QLabel("Размер значков:"), 0, 0);

    auto *sizeRow = new QHBoxLayout;
    sizeRow->setSpacing(6);
    m_sizeSlider = new QSlider(Qt::Horizontal, iconGroup);
    m_sizeSlider->setRange(kIconSizeSliderMin, kIconSizeSliderMax);
    m_sizeSlider->setSingleStep(kIconSizeSliderSingleStep);
    m_sizeSlider->setPageStep(kIconSizeSliderPageStep);
    m_sizeSlider->setValue(s.iconSize);
    m_sizeSlider->setTickPosition(QSlider::NoTicks);
    m_sizeSlider->setTickInterval(kIconSizeSliderTickInterval);

    m_sizeLabel = new QLabel(QString::number(s.iconSize) + " px", iconGroup);
    m_sizeLabel->setMinimumWidth(fontMetrics().horizontalAdvance("96 px") + 12);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_sizeLabel->setText(QString::number(v) + " px");
        emit iconSizeChanged(v);
    });

    sizeRow->addWidget(m_sizeSlider);
    sizeRow->addWidget(m_sizeLabel);
    iconLayout->addLayout(sizeRow, 0, 1);

    iconLayout->addWidget(new QLabel("Шрифт:"), 1, 0);
    auto *fontRow = new QHBoxLayout;
    fontRow->setSpacing(6);
    m_fontFamilyCombo = new QComboBox(iconGroup);
    m_fontFamilyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_fontFamilyCombo->setMinimumContentsLength(10);
    m_fontFamilyCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_fontSizeSpin = new QSpinBox(iconGroup);
    m_fontSizeSpin->setRange(kFontSizeSpinMin, kFontSizeSpinMax);
    m_fontSizeSpin->setValue(s.fontSize);
    m_fontSizeSpin->setSuffix(" pt");
    m_fontSizeSpin->setMinimumWidth(82);

    populateFontList();
    int fontIdx = m_fontFamilyCombo->findText(s.fontFamily, Qt::MatchFixedString);
    if (fontIdx < 0) fontIdx = m_fontFamilyCombo->findText(QFontDatabase::systemFont(QFontDatabase::GeneralFont).family());
    if (fontIdx >= 0)
        m_fontFamilyCombo->setCurrentIndex(fontIdx);

    connect(m_fontFamilyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        emit fontChanged(m_fontFamilyCombo->currentText());
    });
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        emit fontSizeChanged(v);
    });

    fontRow->addWidget(m_fontFamilyCombo, 1);
    fontRow->addWidget(m_fontSizeSpin);
    iconLayout->addLayout(fontRow, 1, 1);

    m_previewCheck = new QCheckBox("Показывать миниатюры изображений", iconGroup);
    m_previewCheck->setChecked(s.showPreview);
    iconLayout->addWidget(m_previewCheck, 2, 0, 1, 2);
    generalLayout->addWidget(iconGroup);

    auto *fileGroup = new QGroupBox("Отображение файлов", generalTab);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->setSpacing(6);

    m_showHiddenCheck = new QCheckBox("Показывать скрытые файлы", fileGroup);
    m_showHiddenCheck->setChecked(s.showHidden);
    fileLayout->addWidget(m_showHiddenCheck);
    generalLayout->addWidget(fileGroup);

    auto *desktopGroup = new QGroupBox("Значки рабочего стола", generalTab);
    auto *desktopLayout = new QVBoxLayout(desktopGroup);
    desktopLayout->setSpacing(6);

    m_showTrashCheck = new QCheckBox("Корзина", desktopGroup);
    m_showTrashCheck->setChecked(s.showTrash);
    desktopLayout->addWidget(m_showTrashCheck);

    m_showComputerCheck = new QCheckBox("Этот компьютер", desktopGroup);
    m_showComputerCheck->setChecked(s.showComputer);
    desktopLayout->addWidget(m_showComputerCheck);

    m_showHomeCheck = new QCheckBox("Домашняя папка", desktopGroup);
    m_showHomeCheck->setChecked(s.showHome);
    desktopLayout->addWidget(m_showHomeCheck);
    generalLayout->addWidget(desktopGroup);

    auto *arrGroup = new QGroupBox("Расположение", generalTab);
    auto *arrLayout = new QGridLayout(arrGroup);
    arrLayout->setColumnStretch(1, 1);
    arrLayout->setVerticalSpacing(8);

    m_autoArrangeCheck = new QCheckBox("Упорядочивать автоматически", arrGroup);
    m_autoArrangeCheck->setChecked(s.autoArrange);
    arrLayout->addWidget(m_autoArrangeCheck, 0, 0, 1, 2);

    arrLayout->addWidget(new QLabel("Сортировать по:"), 1, 0);
    m_sortCombo = new QComboBox(arrGroup);
    m_sortCombo->addItems({"Имени", "Типу", "Дате изменения"});
    m_sortCombo->setCurrentIndex(s.sortOrder);
    arrLayout->addWidget(m_sortCombo, 1, 1);
    generalLayout->addWidget(arrGroup);
    generalLayout->addStretch();

    auto *applicationsTab = new QWidget(tabs);
    auto *applicationsLayout = new QVBoxLayout(applicationsTab);
    applicationsLayout->setContentsMargins(8, 8, 8, 8);
    applicationsLayout->setSpacing(8);

    auto *appGroup = new QGroupBox("Установленные приложения", applicationsTab);
    auto *appLayout = new QVBoxLayout(appGroup);
    appLayout->setSpacing(6);

    auto *description = new QLabel(
        "Выберите приложение, чтобы создать его ярлык на рабочем столе.", appGroup);
    description->setWordWrap(true);
    appLayout->addWidget(description);

    m_appSearchEdit = new QLineEdit(appGroup);
    m_appSearchEdit->setPlaceholderText("Поиск приложений");
    appLayout->addWidget(m_appSearchEdit);

    m_appList = new QListWidget(appGroup);
    m_appList->setAlternatingRowColors(false);
    m_appList->setUniformItemSizes(true);
    appLayout->addWidget(m_appList, 1);

    m_createShortcutButton = new QPushButton("Создать ярлык", appGroup);
    m_createShortcutButton->setEnabled(false);
    appLayout->addWidget(m_createShortcutButton, 0, Qt::AlignRight);

    connect(m_appList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_createShortcutButton->setEnabled(m_appList->currentItem() != nullptr);
    });
    connect(m_appSearchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QString needle = text.trimmed();
        for (int i = 0; i < m_appList->count(); ++i) {
            auto *item = m_appList->item(i);
            const bool matches = needle.isEmpty()
                || item->text().contains(needle, Qt::CaseInsensitive);
            item->setHidden(!matches);
        }
        if (m_appList->currentItem() && m_appList->currentItem()->isHidden())
            m_appList->setCurrentItem(nullptr);
    });
    connect(m_appList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        emit applicationShortcutRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_createShortcutButton, &QPushButton::clicked, this, [this]() {
        if (!m_appList->currentItem()) return;
        emit applicationShortcutRequested(m_appList->currentItem()->data(Qt::UserRole).toString());
    });

    applicationsLayout->addWidget(appGroup);
    applicationsLayout->addStretch();

    auto *scroll = new QScrollArea(tabs);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(generalTab);
    scroll->setMinimumHeight(440);
    tabs->addTab(scroll, "Общие");
    tabs->addTab(applicationsTab, "Приложения");
    if(!applicationsOnly){m_pages=new PageSettings(tabs);tabs->addTab(m_pages,"Страницы");}
    if (applicationsOnly) tabs->removeTab(0);
    contentLayout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(content);
    buttons->setStandardButtons(applicationsOnly ? QDialogButtonBox::Cancel :
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    if (!applicationsOnly) {
        buttons->button(QDialogButtonBox::RestoreDefaults)->setText("По умолчанию");
        buttons->button(QDialogButtonBox::RestoreDefaults)->setToolTip("Вернуть стандартные параметры. Сохранить: Применить или ОК. Файлы и позиции не удаляются.");
        connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &PropertiesDialog::resetDefaults);
        connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]{if(!m_pages||m_pages->commit())emit applied();});
    }
    connect(buttons, &QDialogButtonBox::accepted, this, [this]{if(!m_pages||m_pages->commit())accept();});
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    contentLayout->addWidget(buttons);

    root->addWidget(content);

    populateApplicationsList();
    refreshTheme();
    setFixedHeight(qMin(applicationsOnly ? 560 : 740, screen()->availableGeometry().height()-80));
}

void PropertiesDialog::selectPageMonitor(const QString &monitor){if(m_pages)m_pages->selectMonitor(monitor);}

void PropertiesDialog::populateApplicationsList()
{
    m_appList->clear();

    QStringList appDirs;
    const QString dataHome = qEnvironmentVariableIsEmpty("XDG_DATA_HOME")
        ? QDir::homePath() + "/.local/share"
        : qEnvironmentVariable("XDG_DATA_HOME");
    appDirs << dataHome + "/applications";

    const QStringList dataDirs = qEnvironmentVariableIsEmpty("XDG_DATA_DIRS")
        ? QStringList{"/usr/local/share", "/usr/share"}
        : qEnvironmentVariable("XDG_DATA_DIRS").split(':', Qt::SkipEmptyParts);
    for (const QString &dir : dataDirs)
        appDirs << dir + "/applications";

    QSet<QString> seenDesktopFiles;
    QList<DesktopApplicationEntry> entries;

    for (const QString &dirPath : appDirs) {
        QDir dir(dirPath);
        if (!dir.exists())
            continue;

        const QFileInfoList files = dir.entryInfoList({"*.desktop"}, QDir::Files, QDir::Name);
        for (const QFileInfo &fileInfo : files) {
            if (seenDesktopFiles.contains(fileInfo.fileName()))
                continue;

            const DesktopApplicationEntry entry = readDesktopApplication(fileInfo.absoluteFilePath());
            if (entry.name.isEmpty())
                continue;

            seenDesktopFiles.insert(fileInfo.fileName());
            entries.append(entry);
        }
    }

    std::sort(entries.begin(), entries.end(), [](const DesktopApplicationEntry &a,
                                                 const DesktopApplicationEntry &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    for (const DesktopApplicationEntry &entry : entries) {
        auto *item = new QListWidgetItem(entry.name, m_appList);
        item->setData(Qt::UserRole, entry.desktopFilePath);

        if (!entry.icon.isEmpty()) {
            QIcon icon = QIcon::fromTheme(entry.icon);
            if (icon.isNull()) {
                QFileInfo iconFile(entry.icon);
                if (iconFile.exists())
                    icon = QIcon(iconFile.absoluteFilePath());
            }
            if (!icon.isNull())
                item->setIcon(icon);
        }
    }
}

void PropertiesDialog::populateFontList()
{
    m_fontFamilyCombo->clear();
    QFontDatabase db;
    QStringList fonts = db.families();
    fonts.sort(Qt::CaseInsensitive);
    for (const QString &family : fonts)
        m_fontFamilyCombo->addItem(family);
}

PropertiesDialog::Settings PropertiesDialog::currentSettings() const
{
    Settings s;
    s.iconSize     = m_sizeSlider->value();
    s.showPreview  = m_previewCheck->isChecked();
    s.autoArrange  = m_autoArrangeCheck->isChecked();
    s.showHidden   = m_showHiddenCheck->isChecked();
    s.sortOrder    = m_sortCombo->currentIndex();
    s.showTrash    = m_showTrashCheck->isChecked();
    s.showComputer = m_showComputerCheck->isChecked();
    s.showHome     = m_showHomeCheck->isChecked();
    s.fontFamily   = m_fontFamilyCombo->currentText();
    s.fontSize     = m_fontSizeSpin->value();
    return s;
}

void PropertiesDialog::refreshTheme()
{
    const auto palette=desktopThemePalette();
    setPalette(palette);
    for (auto *child : findChildren<QWidget *>()) child->setPalette(palette);
    QFont uiFont=QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (uiFont.pointSizeF()<10.5) uiFont.setPointSizeF(10.5);
    setFont(uiFont);
    const QString background=palette.window().color().name();
    const QString foreground=palette.windowText().color().name();
    const QString accent=palette.highlight().color().name();
    QColor outline=palette.windowText().color(); outline.setAlpha(40);
    const QString border=QString("rgba(%1,%2,%3,%4)").arg(outline.red()).arg(outline.green()).arg(outline.blue()).arg(outline.alpha());
    setStyleSheet(QString(
        "QWidget { background:%1; color:%2; font-size:%8pt; }"
        "QDialog#PropertiesDialog { background:%1; color:%2; }"
        "#PropBanner { background:transparent; border-bottom:1px solid %4; }"
        "#PropBannerText { color:%2; font-size:14pt; font-weight:600; }"
        "QGroupBox { border:1px solid %4; border-radius:10px; margin-top:12px; padding:14px 12px 10px; }"
        "QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 5px; color:%2; }"
        "QTabWidget::pane { border:0; }"
        "QTabBar::tab { padding:9px 18px; border:0; border-bottom:2px solid transparent; }"
        "QTabBar::tab:selected { border-bottom-color:%3; color:%3; }"
        "QPushButton { padding:7px 14px; border-radius:7px; border:1px solid %4; background:%6; color:%2; }"
        "QPushButton:hover { border-color:%3; }"
        "QPushButton:default { background:%3; color:%7; }"
        "QComboBox, QSpinBox, QLineEdit { padding:5px; background:%5; border:1px solid %4; border-radius:6px; }"
        "QAbstractItemView { background:%5; selection-background-color:%3; selection-color:%7; }"
        "QScrollBar:vertical { background:%1; width:10px; border:0; margin:0; }"
        "QScrollBar::handle:vertical { background:%4; min-height:24px; border-radius:4px; }"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }"
        "QTabBar::tab { color:%2; }"
    ).arg(background,foreground,accent,border,palette.base().color().name(),palette.button().color().name(),palette.highlightedText().color().name(),QString::number(uiFont.pointSizeF())));
}

void PropertiesDialog::resetDefaults()
{
    const Settings defaults;
    m_sizeSlider->setValue(defaults.iconSize);
    int index=m_fontFamilyCombo->findText(defaults.fontFamily);
    if(index>=0) m_fontFamilyCombo->setCurrentIndex(index);
    m_fontSizeSpin->setValue(defaults.fontSize);
    m_previewCheck->setChecked(defaults.showPreview);
    m_showHiddenCheck->setChecked(defaults.showHidden);
    m_showTrashCheck->setChecked(defaults.showTrash);
    m_showComputerCheck->setChecked(defaults.showComputer);
    m_showHomeCheck->setChecked(defaults.showHome);
    m_autoArrangeCheck->setChecked(defaults.autoArrange);
    m_sortCombo->setCurrentIndex(defaults.sortOrder);
}
