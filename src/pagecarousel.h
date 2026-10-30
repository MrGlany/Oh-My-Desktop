#pragma once
#include "desktoplayout.h"
#include <QWidget>
#include <QVariantAnimation>

class QPushButton;
class PageCarousel : public QWidget {
    Q_OBJECT
public:
    explicit PageCarousel(QWidget *parent=nullptr);
    void setPages(const QList<DesktopPage> &pages,const QString &selected,const QString &active);
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    QString selectedPage() const;
    QRectF screenRect(int index) const;
signals:
    void currentChanged(int index);
    void addRequested(int direction);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    void positionControls();
    void drawScreen(class QPainter &p,int index);
    QList<DesktopPage> m_pages;
    QString m_active;
    int m_current=0;
    qreal m_visual=0;
    QPoint m_press;
    bool m_pressed=false;
    QVariantAnimation m_slide;
    QPushButton *m_addLeft,*m_addRight,*m_previous,*m_next,*m_home;
};
