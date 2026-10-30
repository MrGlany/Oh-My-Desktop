#pragma once
#include <QInputDialog>
#include <QLineEdit>
#include <QScreen>
#include <QTimer>
#include <QWindow>

namespace DesktopDialogs {
inline QString getText(QWidget *owner,QScreen *screen,const QString &title,
                       const QString &label,const QString &initial,bool *ok)
{
    // A layer-shell desktop is not an xdg-toplevel transient parent. Keep the
    // input dialog independent, like Properties, and activate its text field.
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
