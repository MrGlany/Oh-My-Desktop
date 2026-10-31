#pragma once
#include "desktoplayout.h"
#include <QWidget>
class QComboBox;
class PageCarousel;
class QLineEdit;
class QPushButton;
class PageSettings:public QWidget {
    Q_OBJECT
public:
    explicit PageSettings(QWidget *parent=nullptr);
    bool commit();
    void selectMonitor(const QString &monitor);
private:
    void refreshMonitors();
    void reload(const QString &selected=QString());
    void add(int direction);
    void move(int direction);
    QString monitor() const;
    void updateSelection(int row);
    DesktopPageSets m_draft;
    QComboBox *m_monitor;
    QComboBox *m_primaryMonitor;
    PageCarousel *m_carousel;
    QLineEdit *m_name;
    QPushButton *m_remove,*m_left,*m_right;
};
