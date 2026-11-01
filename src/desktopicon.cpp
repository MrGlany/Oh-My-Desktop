#include "desktopicon.h"
#include <QPointer>
#include "launcherutil.h"
#include "desktopconstants.h"
#include "applicationicons.h"
#include "trashmonitor.h"

#include <QPainter>
#include <QHideEvent>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QMimeData>
#include <QMimeDatabase>
#include <QUrl>
#include <QIcon>
#include <QApplication>
#include <QFontMetrics>
#include <QImageReader>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QPixmap>
#include <QFile>
#include <QTextStream>

// ── Elided-label helpers ────────────────────────────────────────────────────
// Generates candidate strings for elision, ordered from longest to shortest.
// Each candidate truncates at a natural break point (underscore, hyphen, dot,
// space) and appends the ellipsis character.
static void elisionCandidates(const QString &name, QVector<QString> &out)
{
    out.clear();
    out.append(name);  // full name - check first, no ellipsis needed

    // Collect break positions: AFTER every underscore, hyphen, dot, or space.
    QVector<int> breaks;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name[i];
        if (c == '_' || c == '-' || c == '.' || c == ' ')
            breaks.append(i + 1);
    }
    // Add evenly-spaced mid-word fallbacks so we always have enough candidates.
    const int step = qMax(1, name.size() / 8);
    for (int i = step; i < name.size(); i += step)
        breaks.append(i);

    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

    // Iterate from the END (longest prefixes) to the beginning (shortest).
    // This way the first candidate that fits has the maximum visible text.
    for (int i = breaks.size() - 1; i >= 0; --i) {
        const int b = breaks[i];
        if (b > 0 && b < name.size()) {
            QString cand = name.left(b);
            cand.append(QChar(0x2026));  // …
            out.append(cand);
        }
    }
    // Fallback: three ellipsis chars (always fits).
    out.append(QString(3, QChar(0x2026)));
}

// Returns a 2-line elided version of *name* that fits inside *availWidth*.
// Tries natural break points first (underscores, dots) so the ellipsis never
// appears in the middle of a word.
static QString buildElided(const QFontMetrics &fm, const QString &name,
                            int availWidth, int maxLines)
{
    const QRect testRect(0, 0, availWidth, 9999);
    const int   maxH = fm.lineSpacing() * maxLines;
    const int   flags = Qt::AlignLeft | Qt::TextWrapAnywhere;

    if (fm.boundingRect(testRect, flags, name).height() <= maxH)
        return name;

    QVector<QString> candidates;
    elisionCandidates(name, candidates);

    // The first candidate is the full name (already checked above).  Walk
    // through the progressively shorter ones until one fits.
    for (int i = 1; i < candidates.size(); ++i) {
        if (fm.boundingRect(testRect, flags, candidates[i]).height() <= maxH)
            return candidates[i];
    }
    // Last resort: should always fit (three ellipsis chars).
    return candidates.last();
}


static DesktopEntryData parseDesktopEntry(const QString &filePath) {
    DesktopEntryData data;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return data;
    QTextStream in(&f);
    bool inDesktop = false;
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith('[')) {
            if (line == "[Desktop Entry]") {
                inDesktop = true;
                continue;
            }
            if (inDesktop)
                break;
            continue;
        }
        if (!inDesktop || line.startsWith('#') || !line.contains('=')) continue;
        int eq = line.indexOf('=');
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq+1).trimmed();
        if (key == "Name") data.name = val;
        else if (key == "Exec") data.exec = val;
        else if (key == "Icon") data.icon = val;
    }
    return data;
}

DesktopIcon::DesktopIcon(const QFileInfo &fileInfo, int iconSize, bool showPreview, QWidget *parent)
    : QWidget(parent)
    , m_fileInfo(fileInfo)
    , m_showPreview(showPreview)
    , m_iconDisplaySize(iconSize)
{
    setFixedSize(iconW(), iconH());
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);

    connect(ApplicationIcons::instance(),&ApplicationIcons::changed,this,[this](const QString &path){
        if(path==m_fileInfo.absoluteFilePath()){resolveIcon();update();if(parentWidget())parentWidget()->update();}
    });
    m_renameTimer.setSingleShot(true);
    connect(&m_renameTimer,&QTimer::timeout,this,[this]{
        if(m_selected && isVisible() && !m_pressDragged && QApplication::keyboardModifiers()==Qt::NoModifier)
            emit inlineRenameRequested(this);
    });
    // .desktop file support
    if (fileInfo.suffix().toLower() == "desktop") {
        m_isDesktopFile = true;
        m_desktopEntry = parseDesktopEntry(fileInfo.absoluteFilePath());
        m_tipText = m_desktopEntry.name.isEmpty() ? fileInfo.fileName() : m_desktopEntry.name;
    } else {
        m_tipText = m_fileInfo.fileName();
    }
    resolveIcon();
    m_cachedLabelFont = QFont(); // invalidate: will rebuild on first paint with real font
}

DesktopIcon::DesktopIcon(VirtualType type, int iconSize, QWidget *parent)
    : QWidget(parent)
    , m_virtualType(type)
    , m_iconDisplaySize(iconSize)
{
    setFixedSize(iconW(), iconH());
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::ArrowCursor);
    setMouseTracking(true);
    if (type == VirtualType::Trash) {
        setAcceptDrops(true);
        connect(TrashMonitor::instance(), &TrashMonitor::changed, this, [this](bool) {
            resolveIcon();
            if (parentWidget()) parentWidget()->update();
            update();
        });
    }
    resolveIcon();
    m_tipText = virtualLabel();
    m_cachedLabelFont = QFont(); // invalidate
}

QString DesktopIcon::virtualLabel() const
{
    switch (m_virtualType) {
        case VirtualType::Trash:    return "Корзина";
        case VirtualType::Computer: return "Этот компьютер";
        case VirtualType::Home:     return "Домашняя папка";
        default:                    return {};
    }
}

void DesktopIcon::rebuildLabel(const QFont &font)
{
    const QFontMetrics fm(font);
    QString fullName;
    if (isVirtual()) {
        fullName = virtualLabel();
    } else if (m_isDesktopFile && !m_desktopEntry.name.isEmpty()) {
        fullName = m_desktopEntry.name;
    } else {
        fullName = m_fileInfo.fileName();
    }
    const int avail = iconW() - kIconLabelPadding;
    m_labelElided         = buildElided(fm, fullName, avail, 2);
    // Selected state allows one extra line for more information.  The
    // paintEvent clamps the highlight to widget bounds as a safety net.
    m_labelElidedSelected = buildElided(fm, fullName, avail, 2 + kIconLabelExtraLines);
    m_tipText = fullName;
}

void DesktopIcon::updateSettings(int iconSize, bool showPreview)
{
    m_iconDisplaySize = iconSize;
    m_showPreview     = showPreview;
    setFixedSize(iconW(), iconH());
    m_hasThumbnail = false;
    m_thumbnail    = QPixmap();
    m_cachedIconPixmap = QPixmap();
    m_cachedIconSize = -1;
    resolveIcon();
    m_cachedLabelFont = QFont(); // invalidate
    update();
}

void DesktopIcon::updateSize(int iconSize)
{
    m_iconDisplaySize = iconSize;
    setFixedSize(iconW(), iconH());
    m_cachedIconPixmap = QPixmap();
    m_cachedIconSize = -1;
    m_cachedLabelFont = QFont(); // invalidate
    update();
}

void DesktopIcon::setLabelFont(const QString &family, int pointSize)
{
    m_fontFamily = family;
    m_fontSize = pointSize;
    m_cachedLabelFont = QFont(); // invalidate cache
    update();
}

void DesktopIcon::setGhost(bool ghost)
{
    if (m_ghost == ghost) return;
    m_ghost = ghost;
    update();
}

void DesktopIcon::setDropTarget(bool on)
{
    if (m_dropTarget == on) return;
    m_dropTarget = on;
    update();
}

void DesktopIcon::setCutState(bool cut)
{
    if (m_cut == cut) return;
    m_cut = cut;
    update();
}

void DesktopIcon::resolveIcon()
{
    m_fileInfo.refresh();
    m_unavailableLink = !isVirtual() && m_fileInfo.isSymLink() && !m_fileInfo.exists();
    // Invalidate cached rendered icon - it's about to change
    m_cachedIconPixmap = QPixmap();
    m_cachedIconSize = -1;

    if (m_virtualType != VirtualType::None) {
        switch (m_virtualType) {
            case VirtualType::Trash:
                m_icon = QIcon::fromTheme(TrashMonitor::instance()->full() ? "user-trash-full" : "user-trash",
                         QIcon::fromTheme("user-trash",
                         QIcon::fromTheme("trash")));
                break;
            case VirtualType::Computer:
                m_icon = QIcon::fromTheme("computer",
                         QIcon::fromTheme("system"));
                break;
            case VirtualType::Home:
                m_icon = QIcon::fromTheme("user-home",
                         QIcon::fromTheme("folder-home"));
                break;
            default: break;
        }
        if (m_icon.isNull())
            m_icon = QIcon::fromTheme("folder");
        return;
    }

    if (m_unavailableLink) {
        m_hasThumbnail = false;
        m_thumbnail = QPixmap();
        m_icon = QIcon::fromTheme("folder", QIcon::fromTheme("text-x-generic"));
        return;
    }

    if(!ApplicationIcons::instance()->overridePath(m_fileInfo).isEmpty()){
        const auto custom=ApplicationIcons::instance()->icon(m_fileInfo);
        if(!custom.isNull()){m_hasThumbnail=false;m_thumbnail=QPixmap();m_icon=custom;return;}
    }

    // Custom icon for .desktop files
    if (m_isDesktopFile && !m_desktopEntry.icon.isEmpty()) {
        m_icon = QIcon::fromTheme(m_desktopEntry.icon);
        if (m_icon.isNull()) {
            // Try icon from absolute path
            QFileInfo iconFile(m_desktopEntry.icon);
            if (iconFile.exists())
                m_icon = QIcon(iconFile.absoluteFilePath());
        }
        if (!m_icon.isNull()) return;
    }

    const auto appIcon=ApplicationIcons::instance()->icon(m_fileInfo);
    if(!appIcon.isNull()){m_hasThumbnail=false;m_thumbnail=QPixmap();m_icon=appIcon;return;}

    static const QStringList kImageExts = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp",
        "tiff", "tif", "xpm", "pbm", "pgm", "ppm"
    };

    const QString ext = m_fileInfo.suffix().toLower();
    if (m_showPreview && !m_fileInfo.isDir() && kImageExts.contains(ext)) {
        QImageReader reader(m_fileInfo.absoluteFilePath());
        reader.setAutoTransform(true);
        const QSize srcSize = reader.size();
        if (srcSize.isValid() && !srcSize.isEmpty()) {
            reader.setScaledSize(srcSize.scaled(kIconPreviewScale, kIconPreviewScale, Qt::KeepAspectRatio));
        }
        const QImage img = reader.read();
        if (!img.isNull()) {
            m_thumbnail    = QPixmap::fromImage(img);
            m_hasThumbnail = true;
            return;
        }
    }

    QMimeDatabase mimeDb;
    const QMimeType mimeType = mimeDb.mimeTypeForFile(m_fileInfo);
    QStringList iconNames;

    if (m_fileInfo.isDir()) {
        iconNames << "folder";
    } else {
        // Check by extension first for known executable types.
        // QMimeDatabase uses content sniffing by default, so .exe files
        // without recognizable magic bytes get resolved as text/plain
        // or inode/x-empty. Dolphin uses extension-based lookup, so
        // we do the same for visual parity.
        if (ext == "exe" || ext == "bin" || ext == "run" || ext == "app") {
            iconNames << "application-x-ms-dos-executable"
                      << "application-x-executable";
        }

        if (!mimeType.iconName().isEmpty())
            iconNames << mimeType.iconName();
        if (!mimeType.genericIconName().isEmpty())
            iconNames << mimeType.genericIconName();

        const QString extIcon = "application-x-extension-" + ext;
        if (!ext.isEmpty())
            iconNames << extIcon;

        iconNames << "text-x-generic";
    }

    for (const QString &iconName : iconNames) {
        if (iconName.isEmpty())
            continue;
        m_icon = QIcon::fromTheme(iconName);
        if (!m_icon.isNull())
            return;
    }

    m_icon = QIcon::fromTheme(m_fileInfo.isDir() ? "folder" : "text-x-generic");
}

void DesktopIcon::setSelected(bool selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    if(selected)m_selectedSince.start();
    else {m_selectedSince.invalidate();m_renameTimer.stop();m_inlineCandidate=false;}
    update();
}

void DesktopIcon::refreshLinkState()
{
    if (isVirtual() || !m_fileInfo.isSymLink()) return;
    QFileInfo fresh(m_fileInfo.absoluteFilePath());
    const bool unavailable = fresh.isSymLink() && !fresh.exists();
    if (unavailable == m_unavailableLink) return;
    m_fileInfo = fresh;
    m_hasThumbnail = false;
    m_thumbnail = QPixmap();
    if (m_isDesktopFile) m_desktopEntry = parseDesktopEntry(fresh.absoluteFilePath());
    resolveIcon();
    update();
}

void DesktopIcon::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    if (m_ghost) p.setOpacity(0.4);

    const QRect iconRect((iconW() - m_iconDisplaySize) / 2, kIconLabelMarginTop, m_iconDisplaySize, m_iconDisplaySize);

    // Drop-target highlight (trash)
    if (m_dropTarget) {
        p.fillRect(iconRect.adjusted(-2,-2,2,2), QColor(0, 0, 200, 60));
    }

    // Cut state: dim the icon slightly
    const qreal origOpacity = p.opacity();
    if (m_cut) p.setOpacity(0.5);

    if (m_hasThumbnail) {
        const QSize scaled = m_thumbnail.size().scaled(iconRect.size(), Qt::KeepAspectRatio);
        QRect dst(QPoint(0, 0), scaled);
        dst.moveCenter(iconRect.center());
        p.drawPixmap(dst, m_thumbnail);
    } else {
        const qreal dpr = devicePixelRatioF();
        const qint64 key = m_icon.cacheKey();
        if (m_cachedIconSize != m_iconDisplaySize || !qFuzzyCompare(m_cachedIconDpr, dpr)
            || m_cachedIconKey != key) {
            // Theme engines can return a 32px raster for a 40px request. Ask
            // for twice the target resolution, then downsample once smoothly.
            // Never relabel a small raster with a higher DPR: that loses detail.
            const QPixmap source = m_icon.pixmap(QSize(m_iconDisplaySize*2, m_iconDisplaySize*2),
                                                  dpr, QIcon::Normal, QIcon::Off);
            if (!source.isNull()) {
                const int pixels = qMax(1, qRound(m_iconDisplaySize*dpr));
                QImage image = source.toImage().scaled(pixels, pixels,
                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
                image.setDevicePixelRatio(dpr);
                m_cachedIconPixmap = QPixmap::fromImage(image);
            } else {
                m_cachedIconPixmap = QPixmap();
            }
            m_cachedIconSize = m_iconDisplaySize;
            m_cachedIconDpr = dpr;
            m_cachedIconKey = key;
        }
        if (!m_cachedIconPixmap.isNull()) {
            const QSizeF size = m_cachedIconPixmap.deviceIndependentSize()
                .scaled(QSizeF(iconRect.size()), Qt::KeepAspectRatio);
            QRectF destination(QPointF(0,0), size);
            destination.moveCenter(QRectF(iconRect).center());
            p.drawPixmap(destination, m_cachedIconPixmap, QRectF(m_cachedIconPixmap.rect()));
        } else {
            m_icon.paint(&p, iconRect);
        }
    }

    if (m_unavailableLink) {
        // Draw the lock ourselves: it remains legible with every icon theme.
        const qreal s = qMax(14, m_iconDisplaySize / 2);
        const QRectF badge(iconRect.right()-s+2, iconRect.bottom()-s+2, s, s);
        p.save();
        p.setPen(QPen(palette().color(QPalette::HighlightedText), 1.6));
        p.setBrush(palette().color(QPalette::Highlight));
        p.drawRoundedRect(badge, 4, 4);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(badge.adjusted(s*.30, s*.15, -s*.30, -s*.40), s*.2, s*.2);
        p.setBrush(palette().color(QPalette::HighlightedText));
        p.drawRoundedRect(badge.adjusted(s*.22, s*.43, -s*.22, -s*.17), 1, 1);
        p.restore();
    }

    // Hover brightening: white overlay on the icon image
    if (m_hovered && !m_selected) {
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.fillRect(iconRect, QColor(50, 50, 50, 120));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    // Restore opacity for label drawing
    if (m_cut) p.setOpacity(origOpacity);

    // ── Label ───────────────────────────────────────────────────────────────
    QFont font(m_fontFamily, m_fontSize);
    p.setFont(font);
    QFontMetrics fm(font);

    // Rebuild cache if font changed (e.g. first paint after QSS applied)
    if (font != m_cachedLabelFont) {
        m_cachedLabelFont = font;
        rebuildLabel(font);
    }

    const QString &label = m_selected ? m_labelElidedSelected : m_labelElided;

    // Measure the actual text height to keep the highlight tight.
    // Clamp to widget bottom so we never paint outside our bounds.
    const int textTop  = iconRect.bottom() + kIconLabelMarginTop;
    const int maxTextH = height() - textTop;
    const QRect measureRect(kIconLabelMarginX, textTop, iconW() - kIconLabelPadding, maxTextH);
    const QRect boundRect = fm.boundingRect(measureRect,
        Qt::AlignTop | Qt::AlignHCenter | Qt::TextWrapAnywhere, label);
    const int highlightH = qMin(maxTextH, qMax(fm.height() + kIconLabelMinHeightPadding, boundRect.height() + kIconLabelMinHeightPadding));
    const QRect highlightRect(0, textTop, iconW(), highlightH);
    const QRect textRect = highlightRect.adjusted(kIconLabelMarginX, kIconLabelMarginX, -kIconLabelMarginX, -kIconLabelMarginX);

    if (m_selected) {
        p.fillRect(highlightRect, QColor(0, 0, 128));

        // Simple selection border around the whole icon (image + label)
        const QRect selRect(0, 0, iconW(), highlightRect.bottom() + 1);
        p.setPen(QPen(QColor(255, 255, 255, 180), 1, Qt::DotLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(selRect.adjusted(0, 0, -1, -1));
    } else if (m_hovered) {
        p.fillRect(highlightRect, QColor(0, 0, 128, 80));
    }

    // Shadow + label
    p.setPen(QColor(0, 0, 0, 160));
    p.drawText(textRect.translated(1, 1), Qt::AlignTop | Qt::AlignHCenter | Qt::TextWrapAnywhere, label);
    p.setPen(Qt::white);
    p.drawText(textRect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWrapAnywhere, label);
}

void DesktopIcon::hideEvent(QHideEvent *event){
    m_renameTimer.stop();m_inlineCandidate=false;m_selectedSince.invalidate();
    QWidget::hideEvent(event);
}

void DesktopIcon::mousePressEvent(QMouseEvent *event)
{
    m_renameTimer.stop();
    if (event->button() == Qt::LeftButton) {
        if(m_selected&&!m_selectedSince.isValid())m_selectedSince.start();
        int selected=0;
        if(parentWidget())for(auto *icon:parentWidget()->findChildren<DesktopIcon*>(QString(),Qt::FindDirectChildrenOnly))if(icon->isSelected())++selected;
        const int labelTop=kIconLabelMarginTop+m_iconDisplaySize-1+kIconLabelMarginTop;
        m_inlineCandidate=!isVirtual() && m_selected && selected==1 &&
            event->modifiers()==Qt::NoModifier && event->position().y()>=labelTop &&
            m_selectedSince.isValid() && m_selectedSince.elapsed()>QApplication::doubleClickInterval();
        m_pressDragged=false;m_doubleClickRelease=false;
        m_ctrlAtPress      = event->modifiers() & Qt::ControlModifier;
        m_dragStartPos     = event->globalPosition().toPoint();
        m_dragStartIconPos = pos();
        m_dragOffset       = event->position().toPoint();
        m_dragging         = false;
        m_lastDragPos      = pos();
        emit pressed(this, m_ctrlAtPress);
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        m_inlineCandidate=false;
        m_rightPressed = true;
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void DesktopIcon::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        if (!m_dragging && delta.manhattanLength() > QApplication::startDragDistance()) {
            m_dragging = true;
            m_pressDragged=true;m_inlineCandidate=false;m_renameTimer.stop();
            QPointer<DesktopIcon> alive(this);
            emit dragStarted(this, m_dragOffset);
            if (!alive) return; // The source output may disappear during native drag.
            // Native Wayland drag owns pointer motion and release across outputs.
            m_dragging = false;
            event->accept();
            return;
        }
        if (m_dragging) {
            // Compute new position from delta - avoids mapToGlobal on every event
            const QPoint newPos = m_dragStartIconPos + delta;
            auto *pw = parentWidget();
            if (pw) {
                const int pwW = pw->width();
                const int pwH = pw->height();
                const QPoint clamped(qBound(0, newPos.x(), pwW - width()),
                                     qBound(0, newPos.y(), pwH - height()));
                m_lastDragPos = clamped;
                emit dragging(this, clamped);
            }
        }
    }
    QWidget::mouseMoveEvent(event);
}

void DesktopIcon::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_dragging)
            emit moved(this, m_lastDragPos); // virtual position, not actual widget pos
        const bool wasDrag=m_dragging||m_pressDragged;
        const bool rename=m_inlineCandidate&&!wasDrag&&!m_doubleClickRelease;
        m_inlineCandidate=false;m_doubleClickRelease=false;
        emit released(this,wasDrag,m_ctrlAtPress);
        m_dragging = false;
        if(rename && m_selected)m_renameTimer.start(QApplication::doubleClickInterval()+50);
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        if (m_rightPressed) {
            setSelected(true);
            emit iconContextMenuRequested(this, mapToParent(event->position().toPoint()));
        }
        m_rightPressed = false;
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void DesktopIcon::mouseDoubleClickEvent(QMouseEvent *event)
{
    m_renameTimer.stop();m_inlineCandidate=false;m_doubleClickRelease=true;
    if (event->button() == Qt::LeftButton) {
        if (m_virtualType != VirtualType::None) {
            QString uri;
            switch (m_virtualType) {
                case VirtualType::Trash:    uri = "trash:///"; break;
                case VirtualType::Computer: uri = "computer:///"; break;
                case VirtualType::Home:     uri = QDir::homePath(); break;
                default: break;
            }
            if (!uri.isEmpty())
                LauncherUtil::openLocation(uri);
        } else {
            emit openRequested(m_fileInfo);
        }
        event->accept();return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

// .desktop icon fields (m_isDesktopFile, m_desktopEntry)



void DesktopIcon::contextMenuEvent(QContextMenuEvent *event)
{
    if (event->reason() == QContextMenuEvent::Mouse) {
        event->accept();
        return;
    }
    setSelected(true);
    emit iconContextMenuRequested(this, mapToParent(rect().center()));
    event->accept();
}

void DesktopIcon::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void DesktopIcon::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_virtualType == VirtualType::Trash && event->mimeData()->hasUrls()) {
        m_dropTarget = true;
        update();
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void DesktopIcon::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropTarget = false;
    update();
}

void DesktopIcon::dropEvent(QDropEvent *event)
{
    m_dropTarget = false;
    update();
    if (m_virtualType != VirtualType::Trash) return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            emit fileDropped(url.toLocalFile());
    }
    event->acceptProposedAction();
}

void DesktopIcon::wheelEvent(QWheelEvent *event)
{
    // Forward to parent (DesktopScene) so Ctrl+Scroll zoom works
    // even when the cursor is over an icon.
    event->ignore();
    if (parentWidget())
        QCoreApplication::sendEvent(parentWidget(), event);
}

void DesktopIcon::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
