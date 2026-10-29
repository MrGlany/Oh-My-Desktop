#include "desktoplayout.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QPointer>
#include <QSet>

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
bool DesktopLayout::assign(const QStringList &keys, const QString &screen) {
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    for (const QString &key : keys)
        settings.setValue("owners/" + QString::fromLatin1(key.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)), screen);
    settings.sync();
    if (settings.status() != QSettings::NoError) return false;
    for (const QString &key : keys) m_owners.insert(key, screen);
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
    for (const auto &key:missing)
        settings.remove("owners/"+QString::fromLatin1(key.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    settings.sync();
    if(settings.status()!=QSettings::NoError)return;
    for(const auto &key:missing)m_owners.remove(key);
    emit changed();
}
