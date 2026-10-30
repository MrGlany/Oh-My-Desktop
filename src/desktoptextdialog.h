#pragma once
#include "desktopwindow.h"
#include <LayerShellQt/Window>
#include <QApplication>
#include <QPointer>
#include <QInputDialog>
#include <QLineEdit>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace DesktopDialogs {
class DesktopKeyboardPause {
    using Layer=LayerShellQt::Window;
    struct Entry {QPointer<Layer> layer;Layer::KeyboardInteractivity previous;};
    QList<Entry> m_entries;
public:
    DesktopKeyboardPause(){
        // An on-demand layer that still owns keyboard focus can prevent Hyprland
        // from activating an xdg dialog. Release only our desktop layers, for
        // the duration of this modal dialog; preserve their exact prior modes.
        for(auto *widget:QApplication::topLevelWidgets()){
            auto *desktop=qobject_cast<DesktopWindow*>(widget);
            if(!desktop||!desktop->windowHandle())continue;
            if(auto *layer=Layer::get(desktop->windowHandle())){
                m_entries.append({layer,layer->keyboardInteractivity()});
                layer->setKeyboardInteractivity(Layer::KeyboardInteractivityNone);
                desktop->update();
            }
        }
    }
    ~DesktopKeyboardPause(){
        for(const auto &entry:m_entries)if(entry.layer)entry.layer->setKeyboardInteractivity(entry.previous);
    }
    DesktopKeyboardPause(const DesktopKeyboardPause&)=delete;
    DesktopKeyboardPause& operator=(const DesktopKeyboardPause&)=delete;
};
inline QString getText(QWidget *owner,QScreen *screen,const QString &title,
                       const QString &label,const QString &initial,bool *ok)
{
    // A layer-shell desktop is not an xdg-toplevel transient parent. Keep the
    // input dialog independent, like Properties, and activate its text field.
    DesktopKeyboardPause pause;
    QInputDialog dialog(nullptr);
    dialog.setObjectName("DesktopTextInput");
    dialog.setWindowTitle(title);dialog.setLabelText(label);
    dialog.setInputMode(QInputDialog::TextInput);dialog.setTextValue(initial);
    dialog.setWindowModality(Qt::ApplicationModal);
    if(owner)QObject::connect(owner,&QObject::destroyed,&dialog,&QDialog::reject);
    dialog.ensurePolished();dialog.adjustSize();
    if(screen){dialog.setScreen(screen);dialog.move(screen->availableGeometry().center()-dialog.rect().center());}
    QTimer::singleShot(0,&dialog,[&dialog]{
        dialog.raise();dialog.activateWindow();
        if(auto *window=dialog.windowHandle())window->requestActivate();
        if(auto *edit=dialog.findChild<QLineEdit*>()){edit->setFocus(Qt::OtherFocusReason);edit->selectAll();}
    });
    const bool accepted=dialog.exec()==QDialog::Accepted;
    if(ok)*ok=accepted;
    return accepted?dialog.textValue():QString();
}
}
