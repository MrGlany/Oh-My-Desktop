#pragma once
#include <QObject>
#include <QHash>

// One owner per desktop entry. Missing outputs are displayed temporarily on the
// primary output without discarding the saved owner for when it reconnects.
class DesktopLayout : public QObject {
    Q_OBJECT
public:
    static DesktopLayout *instance();
    static QString screenName(const class QScreen *screen);
    QString primaryScreen() const;
    QString owner(const QString &key) const;
    bool assign(const QStringList &keys, const QString &screen);
    void rememberDefaults(const QStringList &keys);
    void forgetMissing(const QStringList &existing);
signals:
    void changed();
private:
    explicit DesktopLayout(QObject *parent);
    QHash<QString, QString> m_owners;
    QString m_path;
};
