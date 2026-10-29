#pragma once

#include <QWidget>
#include <QScreen>
#include <QPointer>

class DesktopScene;

class DesktopWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DesktopWindow(QScreen *screen, QWidget *parent = nullptr);
    ~DesktopWindow() override = default;

    void setKeyboardInteractive(bool on);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupLayerShell();

    DesktopScene *m_scene = nullptr;
    QPointer<QScreen> m_screen;
};
