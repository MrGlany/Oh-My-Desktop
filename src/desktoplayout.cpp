#include "desktoplayout.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QPointer>
#include <QSet>

static QString encoded(const QString &s) { return QString::fromLatin1(s.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)); }

DesktopLayout *DesktopLayout::instance() {
    static QPointer<DesktopLayout> layout;
    if (!layout) layout = new DesktopLayout(QCoreApplication::instance());
    return layout;
}
QString DesktopLayout::screenName(const QScreen *screen) {
    return screen && !screen->name().isEmpty() ? screen->name() : QStringLiteral("default");
}
DesktopLayout::DesktopLayout(QObject *parent) : QObject(parent) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    m_path = dir + "/monitor-layout.ini";
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup("owners");
    for (const QString &key : settings.childKeys())
        m_owners.insert(QString::fromUtf8(QByteArray::fromBase64(key.toLatin1(), QByteArray::Base64UrlEncoding)), settings.value(key).toString());
    settings.endGroup();
    settings.beginGroup("entryPages");
    for (const auto &key : settings.childKeys())
        m_entryPages.insert(QString::fromUtf8(QByteArray::fromBase64(key.toLatin1(), QByteArray::Base64UrlEncoding)), settings.value(key).toString());
    settings.endGroup();
    settings.beginGroup("desktopPages");
    for (const auto &key : settings.childGroups()) {
        settings.beginGroup(key);
        const QString screen = QString::fromUtf8(QByteArray::fromBase64(key.toLatin1(), QByteArray::Base64UrlEncoding));
        QList<DesktopPage> list;
        QSet<QString> seen;
        for (const auto &id : settings.value("order").toStringList()) {
            if (id.isEmpty() || id.contains('/') || seen.contains(id)) continue;
            seen.insert(id);
            list.append({id,settings.value("names/"+id,id=="main" ? "Основная" : "Страница").toString()});
        }
        if (!seen.contains("main")) list.prepend({"main","Основная"});
        m_pages.insert(screen,list);
        const QString active=settings.value("active","main").toString();
        m_activePages.insert(screen,seen.contains(active) ? active : "main");
        settings.endGroup();
    }
    settings.endGroup();
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this]{emit changed();});
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this]{emit changed();});
}
QString DesktopLayout::primaryScreen() const {
    // Hyprland has no persistent primary-output flag. Match this installation's
    // main output, with an optional explicit override and a safe Qt fallback.
    const QString preferred = qEnvironmentVariable("OMD_PRIMARY_MONITOR", "DP-1");
    for (auto *screen : QGuiApplication::screens())
        if (screenName(screen) == preferred) return preferred;
    return screenName(QGuiApplication::primaryScreen());
}
QString DesktopLayout::owner(const QString &key) const {
    const QString saved = m_owners.value(key, primaryScreen());
    for (auto *screen : QGuiApplication::screens())
        if (screenName(screen) == saved) return saved;
    return primaryScreen();
}
QString DesktopLayout::focusedScreen() const {
    for(auto *screen:QGuiApplication::screens())if(screenName(screen)==m_focusedScreen)return m_focusedScreen;
    return primaryScreen();
}
void DesktopLayout::setFocusedScreen(const QString &screen) {
    for(auto *s:QGuiApplication::screens())if(screenName(s)==screen){m_focusedScreen=screen;return;}
}
bool DesktopLayout::assign(const QStringList &keys, const QString &screen, const QString &page) {
    bool valid=false; for (const auto &p:pages(screen)) if (p.id==page) valid=true;
    if (!valid || screen.isEmpty()) return false;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    for (const QString &key : keys) {
        settings.setValue("owners/"+encoded(key),screen);
        settings.setValue("entryPages/"+encoded(key),page);
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) return false;
    for (const QString &key : keys) { m_owners.insert(key, screen); m_entryPages.insert(key,page); }
    emit changed();
    return true;
}

void DesktopLayout::rememberDefaults(const QStringList &keys) {
    QStringList missing;
    for (const QString &key : keys) if (!m_owners.contains(key)) missing.append(key);
    if (!missing.isEmpty()) assign(missing, primaryScreen());
}

void DesktopLayout::forgetMissing(const QStringList &existing) {
    const QSet<QString> names(existing.cbegin(), existing.cend());
    QStringList missing;
    for (auto it=m_owners.cbegin();it!=m_owners.cend();++it)
        if (it.key()!="__trash__" && it.key()!="__computer__" && it.key()!="__home__" && !names.contains(it.key())) missing.append(it.key());
    if (missing.isEmpty()) return;
    QSettings settings(m_path,QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    for (const auto &key:missing) { settings.remove("owners/"+encoded(key)); settings.remove("entryPages/"+encoded(key)); }
    settings.sync();
    if(settings.status()!=QSettings::NoError)return;
    for(const auto &key:missing) {m_owners.remove(key);m_entryPages.remove(key);}
    emit changed();
}

QList<DesktopPage> DesktopLayout::pages(const QString &screen) const {
    return m_pages.value(screen,{{"main","Основная"}});
}
DesktopPageSets DesktopLayout::pageSets() const {
    auto sets=m_pages;
    for (auto *s:QGuiApplication::screens()) if (!sets.contains(screenName(s))) sets.insert(screenName(s),pages(screenName(s)));
    return sets;
}
QString DesktopLayout::activePage(const QString &screen) const { return m_activePages.value(screen,"main"); }
QString DesktopLayout::entryPage(const QString &key) const {
    const QString savedMonitor=m_owners.value(key,primaryScreen());
    // A temporarily missing output appears on the primary's main page.
    if (owner(key)!=savedMonitor) return "main";
    const QString id=m_entryPages.value(key,"main");
    for (const auto &p:pages(savedMonitor)) if (p.id==id) return id;
    return "main";
}
bool DesktopLayout::visible(const QString &key,const QString &screen) const {
    return owner(key)==screen && entryPage(key)==activePage(screen);
}
bool DesktopLayout::activatePage(const QString &screen,const QString &page) {
    bool valid=false;for(const auto &p:pages(screen))if(p.id==page)valid=true;
    if (!valid) return false;
    if (activePage(screen)==page) return true;
    QSettings cfg(m_path,QSettings::IniFormat);cfg.setAtomicSyncRequired(true);
    cfg.setValue("desktopPages/"+encoded(screen)+"/active",page);
    // Persist order too, including the implicit main page on old installations.
    QStringList order;for(const auto &p:pages(screen))order.append(p.id);
    cfg.setValue("desktopPages/"+encoded(screen)+"/order",order);cfg.sync();
    if(cfg.status()!=QSettings::NoError)return false;
    m_activePages.insert(screen,page);emit changed();return true;
}
bool DesktopLayout::replacePages(const DesktopPageSets &sets) {
    // Validate the entire edit before writing anything. Never drop an output.
    for(const auto &screen:pageSets().keys())if(!sets.contains(screen))return false;
    for(auto it=sets.cbegin();it!=sets.cend();++it) {
        QSet<QString> ids;
        if(it.key().isEmpty() || it.value().isEmpty() || it.value().size()>64)return false;
        for(const auto &p:it.value()) {
            if(p.id.isEmpty() || p.id.contains('/') || p.id.contains('\\') || ids.contains(p.id) || p.name.trimmed().isEmpty())return false;
            ids.insert(p.id);
        }
        if(!ids.contains("main"))return false;
    }
    auto active=m_activePages;auto entryPages=m_entryPages;
    QSettings cfg(m_path,QSettings::IniFormat);cfg.setAtomicSyncRequired(true);
    for(auto it=sets.cbegin();it!=sets.cend();++it) {
        QSet<QString> ids;QStringList order;
        const QString prefix="desktopPages/"+encoded(it.key())+"/";
        for(const auto &p:it.value()) {ids.insert(p.id);order.append(p.id);cfg.setValue(prefix+"names/"+p.id,p.name.trimmed());}
        cfg.setValue(prefix+"order",order);
        if(!ids.contains(active.value(it.key(),"main")))active[it.key()]="main";
        cfg.setValue(prefix+"active",active.value(it.key(),"main"));
        // Removing a page returns its icons to main. Files are untouched.
        for(auto owner=m_owners.cbegin();owner!=m_owners.cend();++owner) {
            if(owner.value()==it.key() && !ids.contains(entryPages.value(owner.key(),"main"))) {
                entryPages[owner.key()]="main";cfg.setValue("entryPages/"+encoded(owner.key()),"main");
            }
        }
    }
    cfg.sync();if(cfg.status()!=QSettings::NoError)return false;
    m_pages=sets;m_activePages=active;m_entryPages=entryPages;emit changed();return true;
}
