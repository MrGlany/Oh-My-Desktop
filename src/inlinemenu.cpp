#include "inlinemenu.h"

#include <QPainter>
#include <QCursor>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QApplication>
#include <QFontDatabase>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QIcon>
#include <QHash>
#include <QTimer>
#include <QEnterEvent>

// ---------------------------------------------------------------------------
// InlineMenu - child-widget context menu (no QMenu, avoids Wayland grab issues)
// ---------------------------------------------------------------------------

static QString menuText(const QString &text) {
    static const QHash<QString, QString> translations{
        {"Name", "По имени"},
        {"Type", "По типу"},
        {"Date Modified", "По дате изменения"},
        {"Folder", "Папку"},
        {"Document", "Текстовый документ"},
        {"New", "Создать"},
        {"Open in Terminal", "Открыть в терминале"},
        {"Arrange Icons By", "Сортировать значки"},
        {"Auto Arrange Icons", "Упорядочивать автоматически"},
        {"Show Hidden Files", "Показывать скрытые файлы"},
        {"Refresh", "Обновить"},
        {"System", "Система"},
        {"Properties...", "Свойства…"},
        {"Properties…", "Свойства…"},
        {"Open", "Открыть"},
        {"Extract here", "Распаковать здесь"},
        {"Copy", "Копировать"},
        {"Cut", "Вырезать"},
        {"Paste", "Вставить"},
        {"Rename", "Переименовать"},
        {"Delete", "Переместить в корзину"},
        {"Lock", "Заблокировать"},
        {"Log Out", "Выйти из сеанса"},
        {"Suspend", "Спящий режим"},
        {"Restart", "Перезагрузить"},
        {"Shut Down", "Выключить"},
    };
    return translations.value(text, text);
}

InlineMenu::InlineMenu(QWidget *parent) : QWidget(parent)
{
    m_submenuCloseTimer = new QTimer(this);
    m_submenuCloseTimer->setSingleShot(true);
    m_submenuCloseTimer->setInterval(300);
    connect(m_submenuCloseTimer, &QTimer::timeout, this, [this] {
        if (!m_activeSubmenu) return;
        const QPoint point = parentWidget()->mapFromGlobal(QCursor::pos());
        if (m_activeSubmenu->hitMenu(point)) return;
        m_activeSubmenu->_hideTreeImmediate();
        m_activeSubmenu = nullptr;
        m_hover = -1;
        update();
    });
    setMouseTracking(true);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    refreshTheme();
    auto *watcher = new QFileSystemWatcher(this);
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    watcher->addPath(config);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, [this] { refreshTheme(); update(); });
    watcher->addPath(config + "/kdeglobals");
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [this, watcher, config] {
        refreshTheme(); update();
        if (!watcher->files().contains(config + "/kdeglobals")) watcher->addPath(config + "/kdeglobals");
    });

    // Opacity effect for fade in/out
    auto *effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(1.0);
    setGraphicsEffect(effect);
    m_opacityEffect = effect;

    hide();
}

void InlineMenu::refreshTheme() {
    QPalette colors = QApplication::palette();
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kdeglobals";
    QSettings settings(path, QSettings::IniFormat);
    auto read = [&](const QString &key, QColor fallback) {
        QStringList rgb = settings.value(key).toStringList();
        if (rgb.size() == 1) rgb = rgb.first().split(',');
        if (rgb.size() < 3) return fallback;
        bool a, b, c;
        int r=rgb[0].toInt(&a), g=rgb[1].toInt(&b), v=rgb[2].toInt(&c);
        return a && b && c ? QColor(r,g,v) : fallback;
    };
    colors.setColor(QPalette::Window, read("Colors:Window/BackgroundNormal", colors.window().color()));
    colors.setColor(QPalette::WindowText, read("Colors:Window/ForegroundNormal", colors.windowText().color()));
    colors.setColor(QPalette::Highlight, read("Colors:Selection/BackgroundNormal", colors.highlight().color()));
    colors.setColor(QPalette::HighlightedText, read("Colors:Selection/ForegroundNormal", colors.highlightedText().color()));
    colors.setColor(QPalette::Mid, read("Colors:Window/ForegroundInactive", colors.mid().color()));
    setPalette(colors);
    QFont f = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (f.pointSizeF() < 10.5) f.setPointSizeF(10.5);
    setFont(f);
    m_itemH = qMax(kItemH, QFontMetrics(f).height()+14);
}

void InlineMenu::setRoot(InlineMenu *root) {
    m_root = root;
    for (auto &item : m_items) if (item.submenu) item.submenu->setRoot(root);
}

void InlineMenu::clear() {
    m_submenuCloseTimer->stop();
    if (m_activeSubmenu) m_activeSubmenu->_hideTreeImmediate();
    m_items.clear(); m_hover = -1; m_activeSubmenu = nullptr;
}

void InlineMenu::addAction(const QString &text, std::function<void()> fn) {
    m_items.push_back({text, false, false, false, false, nullptr, std::move(fn)});
}

void InlineMenu::addCheckable(const QString &text, bool checked, std::function<void()> fn) {
    m_items.push_back({text, false, true, checked, false, nullptr, std::move(fn)});
}

void InlineMenu::addSub(const QString &text, InlineMenu *sub) {
    sub->setRoot(m_root ? m_root : this);
    sub->m_parentMenu = this;
    m_items.push_back({text, false, false, false, true, sub, nullptr});
}

void InlineMenu::addSep() { m_items.push_back({"", true}); }

void InlineMenu::popup(const QPoint &pos, const QRect &available) {
    m_submenuCloseTimer->stop();
    refreshTheme();
    QFontMetrics fm(font());
    int w = kMinW;
    for (auto &it : m_items)
        if (!it.isSep)
            w = qMax(w, fm.horizontalAdvance(menuText(it.label)) + kPadL + kPadR + 4);
    int h = 16;
    for (auto &it : m_items) h += it.isSep ? kSepH : m_itemH;
    m_available = available.isValid() ? available
                : QRect(0, 0, parentWidget()->width(), parentWidget()->height());
    QPoint p = pos;
    // Horizontal: shift left if it would overflow the right edge
    if (p.x() + w > m_available.right())  p.rx() = qMax(m_available.left(), m_available.right() - w);
    // Vertical: open upward when there is more space above than below,
    // or when the menu would overflow downward. This prevents any edge-case
    // narrow band where the menu is clipped by the panel.
    const int spaceBelow = m_available.bottom() - p.y();
    const int spaceAbove = p.y() - m_available.top();
    if (h > spaceBelow && spaceAbove > spaceBelow)
        p.ry() = qMax(m_available.top(), p.y() - h);
    // Final clamp: keep fully inside the available area
    p.ry() = qBound(m_available.top(), p.y(), qMax(m_available.top(), m_available.bottom() - h));
    resize(w, h);
    move(p);
    raise();
    m_closing = false;

    // Reset opacity and show
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(0.0);
    }
    show();
    startFadeIn();
}

void InlineMenu::closeAll() {
    // Delegate to root - avoids double-close
    InlineMenu *root = m_root ? m_root : this;
    root->_closeTree();
}

void InlineMenu::closeAllImmediate() {
    InlineMenu *root = m_root ? m_root : this;
    root->_hideTreeImmediate();
}

void InlineMenu::_closeTree() {
    m_submenuCloseTimer->stop();
    m_activeSubmenu = nullptr;
    for (auto &it : m_items)
        if (it.submenu) it.submenu->_hideTreeImmediate();
    startFadeOut([this]() { /* lambda keeps capture simple */ });
}

void InlineMenu::_hideTreeImmediate() {
    for (auto &it : m_items)
        if (it.submenu) it.submenu->_hideTreeImmediate();
    _hideSelf();
}

void InlineMenu::_hideSelf() {
    m_submenuCloseTimer->stop();
    ++m_fadeSeq;
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        m_opacityAnim->deleteLater();
        m_opacityAnim = nullptr;
    }
    m_closing = false;
    m_activeSubmenu = nullptr;
    m_hover = -1;
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(1.0);
    }
    hide();
}

bool InlineMenu::hitMenu(const QPoint &scenePos, int depth) const {
    if (depth > 16) return false;  // guard against circular submenu refs
    if (isVisible() && geometry().contains(scenePos)) return true;
    for (auto &it : m_items)
        if (it.submenu && it.submenu->hitMenu(scenePos, depth + 1)) return true;
    return false;
}

int InlineMenu::hitTest(const QPoint &p) const {
    int y = 8;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        int h = m_items[i].isSep ? kSepH : m_itemH;
        if (p.x() >= 0 && p.x() < width() && !m_items[i].isSep && p.y() >= y && p.y() < y + h) return i;
        y += h;
    }
    return -1;
}

QRect InlineMenu::itemRect(int idx) const {
    int y = 8;
    for (int i = 0; i < (int)m_items.size(); ++i) {
        int h = m_items[i].isSep ? kSepH : m_itemH;
        if (i == idx) return {0, y, width(), h};
        y += h;
    }
    return {};
}

// ── Fade animations ─────────────────────────────────────────────────────────

static constexpr int kFadeMs = 170;

void InlineMenu::startFadeIn() {
    if (!m_opacityEffect) return;
    ++m_fadeSeq;
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        m_opacityAnim->deleteLater();
        m_opacityAnim = nullptr;
    }
    auto *anim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_opacityAnim = anim;
    m_closing = false;
    anim->setDuration(kFadeMs);
    anim->setStartValue(m_opacityEffect->opacity());
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
        if (m_opacityAnim == anim)
            m_opacityAnim = nullptr;
    });
    anim->start(QPropertyAnimation::DeleteWhenStopped);
}

void InlineMenu::startFadeOut(std::function<void()> onFinished) {
    if (m_closing) return;
    if (!m_opacityEffect) {
        hide();
        if (onFinished) onFinished();
        return;
    }
    const int fadeSeq = ++m_fadeSeq;
    if (m_opacityAnim) {
        m_opacityAnim->stop();
        m_opacityAnim->deleteLater();
        m_opacityAnim = nullptr;
    }
    auto *anim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_opacityAnim = anim;
    m_closing = true;
    anim->setDuration(kFadeMs);
    anim->setStartValue(m_opacityEffect->opacity());
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(anim, &QPropertyAnimation::finished, this, [this, onFinished, fadeSeq, anim]() {
        if (m_opacityAnim == anim)
            m_opacityAnim = nullptr;
        if (fadeSeq != m_fadeSeq)
            return;
        m_closing = false;
        hide();
        if (onFinished) onFinished();
    });
    anim->start(QPropertyAnimation::DeleteWhenStopped);
}

void InlineMenu::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette colors = palette();
    QColor border = colors.windowText().color(); border.setAlpha(36);
    p.setPen(QPen(border, 1));
    p.setBrush(colors.window());
    p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5), 14, 14);
    auto iconName = [](const QString &label) -> QString {
        if (label.contains("Terminal")) return "utilities-terminal";
        if (label == "Ярлык приложения…") return "application-x-executable";
        if (label.contains("Folder")) return "folder-new";
        if (label.contains("Document") || label == "New") return "document-new";
        if (label.contains("Refresh")) return "view-refresh";
        if (label.contains("Properties")) return "document-properties";
        if (label.contains("Paste")) return "edit-paste";
        if (label.contains("Copy")) return "edit-copy";
        if (label.contains("Cut")) return "edit-cut";
        if (label.contains("Delete") || label.contains("Trash") || label.contains("Очистить")) return "user-trash";
        if (label.contains("Rename")) return "edit-rename";
        if (label.contains("Open") || label.contains("Открыть")) return "document-open";
        if (label.contains("Arrange")) return "view-sort-ascending";
        if (label.contains("Hidden")) return "view-hidden";
        if (label.contains("System")) return "preferences-system";
        return {};
    };
    for (int i = 0; i < m_items.size(); ++i) {
        const auto &item = m_items[i];
        const QRect row = itemRect(i);
        if (item.isSep) {
            QColor separator=colors.windowText().color(); separator.setAlpha(24);
            p.setPen(separator);
            p.drawLine(14,row.center().y(),width()-14,row.center().y());
            continue;
        }
        const bool selected = i == m_hover;
        const QColor text = selected ? colors.highlightedText().color() : colors.windowText().color();
        if (selected) {
            p.setPen(Qt::NoPen); p.setBrush(colors.highlight());
            p.drawRoundedRect(row.adjusted(7,1,-7,-1),8,8);
        }
        p.setPen(text);
        if (item.checkable && item.checked) {
            p.setPen(QPen(text,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const int y=row.center().y();
            p.drawLine(17,y,21,y+4); p.drawLine(21,y+4,28,y-5);
        } else if (!item.checkable) {
            QPixmap icon = QIcon::fromTheme(iconName(item.label)).pixmap(18,18);
            if (!icon.isNull()) {
                QPainter tint(&icon); tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(icon.rect(), text); tint.end();
                p.drawPixmap(15,row.center().y()-9,icon);
            }
        }
        p.setPen(text);
        p.drawText(QRect(kPadL,row.top(),width()-kPadL-kPadR,row.height()),Qt::AlignVCenter,menuText(item.label));
        if (item.isSub) {
            p.setPen(QPen(text,1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
            const int x=width()-21,y=row.center().y();
            p.drawLine(x-2,y-4,x+2,y);p.drawLine(x+2,y,x-2,y+4);
        }
    }
}

void InlineMenu::cancelSubmenuClose() {
    m_submenuCloseTimer->stop();
    if (m_parentMenu) m_parentMenu->cancelSubmenuClose();
}

void InlineMenu::scheduleSubmenuClose() {
    if (m_activeSubmenu && !m_submenuCloseTimer->isActive())
        m_submenuCloseTimer->start();
}

void InlineMenu::enterEvent(QEnterEvent *ev) {
    cancelSubmenuClose();
    QWidget::enterEvent(ev);
}

void InlineMenu::mouseMoveEvent(QMouseEvent *ev) {
    // Each menu owns only its immediate child. A child must never close itself
    // merely because the pointer reached one of its ordinary action rows.
    if (m_parentMenu) m_parentMenu->cancelSubmenuClose();
    const int idx = hitTest(ev->pos());
    const bool changed = idx != m_hover;
    m_hover = idx;
    if (changed) update();
    if (idx >= 0 && m_items[idx].isSub && m_items[idx].submenu) {
        m_submenuCloseTimer->stop();
        auto *child = m_items[idx].submenu;
        if (m_activeSubmenu && m_activeSubmenu != child)
            m_activeSubmenu->_hideTreeImmediate();
        if (m_activeSubmenu != child || !child->isVisible()) {
            const QRect row = itemRect(idx);
            child->popup(mapToParent(QPoint(width()-1, row.top()-8)), m_available);
            if (child->x() < x()+width()-8)
                child->move(qMax(m_available.left(), x()-child->width()+1), child->y());
        }
        m_activeSubmenu = child;
    } else {
        // Allow crossing rounded edges and moving diagonally into the child.
        scheduleSubmenuClose();
    }
}

void InlineMenu::leaveEvent(QEvent *ev) {
    QWidget::leaveEvent(ev);
    for (auto *owner = this; owner; owner = owner->m_parentMenu.data())
        owner->scheduleSubmenuClose();
}

void InlineMenu::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::LeftButton) {
        int idx = hitTest(ev->pos());
        if (idx >= 0 && !m_items[idx].isSep && !m_items[idx].isSub) {
            auto fn = m_items[idx].fn;
            closeAll();
            if (fn) fn();
            return;
        }
    }
    ev->ignore();
}
