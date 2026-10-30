#pragma once
#include <QObject>
#include <QHash>
#include <QMap>
#include <QStringList>

struct DesktopPage {
    QString id;
    QString name;
};
using DesktopPageSets = QMap<QString, QList<DesktopPage>>;

// One owner per desktop entry. Missing outputs are displayed temporarily on the
// primary output without discarding the saved owner for when it reconnects.
class DesktopLayout : public QObject {
    Q_OBJECT
public:
    static DesktopLayout *instance();
    static QString screenName(const class QScreen *screen);
    QString primaryScreen() const;
    QString focusedScreen() const;
    void setFocusedScreen(const QString &screen);
    QString owner(const QString &key) const;
    bool assign(const QStringList &keys, const QString &screen, const QString &page = "main");
    QList<DesktopPage> pages(const QString &screen) const;
    DesktopPageSets pageSets() const;
    QString activePage(const QString &screen) const;
    QString entryPage(const QString &key) const;
    bool visible(const QString &key, const QString &screen) const;
    bool activatePage(const QString &screen, const QString &page);
    bool replacePages(const DesktopPageSets &sets);
    void rememberDefaults(const QStringList &keys);
    void forgetMissing(const QStringList &existing);
signals:
    void changed();
private:
    explicit DesktopLayout(QObject *parent);
    QHash<QString, QString> m_owners;
    QHash<QString, QString> m_entryPages;
    QHash<QString, QString> m_activePages;
    DesktopPageSets m_pages;
    QString m_path;
    QString m_focusedScreen;
};
