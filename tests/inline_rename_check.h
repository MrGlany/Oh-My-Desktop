#pragma once
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QInputDialog>

static int inlineRenameCheck(){
    const QString path=qEnvironmentVariable("OMD_DESKTOP_DIR");
    if(!path.contains("inline-rename-check"))return 11;
    QApplication::setDoubleClickInterval(100);
    QDir().mkpath(path);
    auto write=[](const QString &name,const QByteArray &bytes){QFile f(name);return f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(bytes)==bytes.size();};
    if(!write(path+"/Original.txt",QByteArray("preserve\x00me\n",12))||!write(path+"/taken.txt","existing target\n"))return 12;
    {
        // Widget-only checks: no file opener or native drag is connected.
        QWidget parent;parent.resize(400,250);
        DesktopIcon file(QFileInfo(path+"/Original.txt"),40,false,&parent);file.show();parent.show();
        QSignalSpy opens(&file,&DesktopIcon::openRequested),renames(&file,&DesktopIcon::inlineRenameRequested),drags(&file,&DesktopIcon::dragStarted);
        file.setSelected(true);QTest::qWait(140);
        const QPoint label(file.width()/2,55);
        QTest::mouseClick(&file,Qt::LeftButton,Qt::NoModifier,label);
        QTest::mouseDClick(&file,Qt::LeftButton,Qt::NoModifier,label);QTest::qWait(190);
        if(opens.count()!=1||!renames.isEmpty())return 13;
        QTest::mouseClick(&file,Qt::LeftButton,Qt::NoModifier,label);
        file.setSelected(false);QTest::qWait(190);if(!renames.isEmpty())return 14;
        file.setSelected(true);QTest::qWait(140);
        QTest::mousePress(&file,Qt::LeftButton,Qt::NoModifier,label);
        const QPoint move=label+QPoint(40,0);
        QMouseEvent drag(QEvent::MouseMove,move,file.mapToGlobal(move),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&file,&drag);QTest::mouseRelease(&file,Qt::LeftButton,Qt::NoModifier,move);QTest::qWait(190);
        if(drags.count()!=1||!renames.isEmpty())return 15;
        {
            DesktopIcon second(QFileInfo(path+"/taken.txt"),40,false,&parent);second.move(120,0);second.show();second.setSelected(true);
            file.setSelected(true);QTest::qWait(140);
            QTest::mouseClick(&file,Qt::LeftButton,Qt::NoModifier,label);QTest::qWait(190);
            if(!renames.isEmpty())return 44;
        }
        QTest::mouseClick(&file,Qt::LeftButton,Qt::ControlModifier,label);QTest::qWait(190);if(!renames.isEmpty())return 16;
        QTest::mouseClick(&file,Qt::LeftButton,Qt::NoModifier,QPoint(35,20));QTest::qWait(190);if(!renames.isEmpty())return 17;
    }
    auto *screen=new LayoutTestScreen("DP-1",0);QWindowSystemInterface::handleScreenAdded(screen);
    const int result=[&]()->int{
        DesktopScene scene(screen->screen());scene.resize(640,480);scene.show();scene.activateWindow();QTest::qWait(600);
        auto editor=[&](){return scene.findChild<QPlainTextEdit*>("InlineRenameEditor");};
        auto start=[&](const QString &name)->QPlainTextEdit*{
            auto *icon=layoutIcon(scene,name);if(!icon)return nullptr;
            // First select, then a distinct slow click on the label.
            QTest::mouseClick(icon,Qt::LeftButton,Qt::NoModifier,QPoint(icon->width()/2,55));QTest::qWait(140);
            QTest::mouseClick(icon,Qt::LeftButton,Qt::NoModifier,QPoint(icon->width()/2,55));QTest::qWait(190);
            return editor();
        };
        auto *original=layoutIcon(scene,"Original.txt");if(!original)return 18;
        const QPoint position=original->pos();
        QTest::mouseClick(original,Qt::LeftButton,Qt::NoModifier,QPoint(original->width()/2,55));QTest::qWait(190);
        if(editor())return 19;
        QTest::mouseClick(original,Qt::LeftButton,Qt::NoModifier,QPoint(original->width()/2,55));QTest::qWait(190);
        auto *edit=editor();if(!edit||!edit->hasFocus()||edit->textCursor().selectedText()!="Original")return 20;
        auto *box=scene.findChild<QWidget*>("InlineRenamePanel");if(!box||!scene.rect().contains(box->geometry()))return 21;
        if(qEnvironmentVariableIsSet("OMD_INLINE_PREVIEW"))scene.grab().save(qEnvironmentVariable("OMD_INLINE_PREVIEW"));
        edit->setPlainText("Cancelled.txt");QTest::keyClick(edit,Qt::Key_Escape);QTest::qWait(300);
        if(editor()||!QFileInfo::exists(path+"/Original.txt")||QFileInfo::exists(path+"/Cancelled.txt"))return 22;
        edit=start("Original.txt");if(!edit)return 23;
        edit->setPlainText("../escape.txt");QTest::keyClick(edit,Qt::Key_Return);
        if(!editor()||QFileInfo::exists(path+"/../escape.txt"))return 24;
        edit->setPlainText("taken.txt");QTest::keyClick(edit,Qt::Key_Return);
        QFile taken(path+"/taken.txt");if(!taken.open(QIODevice::ReadOnly)||taken.readAll()!="existing target\n"||!editor())return 25;
        auto *error=scene.findChild<QLabel*>("InlineRenameError");if(!error||!error->isVisible())return 26;
        edit->setPlainText("Новое имя.txt");
        if(!write(path+"/unrelated.txt","other file\n"))return 27;
        QTest::qWait(600);if(editor()!=edit||edit->toPlainText()!="Новое имя.txt")return 28;
        QTest::keyClick(edit,Qt::Key_Return);QTest::qWait(600);
        auto *renamed=layoutIcon(scene,"Новое имя.txt");
        QFile source(path+"/Новое имя.txt");
        if(editor()||QFileInfo::exists(path+"/Original.txt")||!renamed||renamed->pos()!=position||!renamed->isSelected()||!source.open(QIODevice::ReadOnly)||source.readAll()!=QByteArray("preserve\x00me\n",12)){
            fprintf(stderr,"rename state: editor=%d old=%d icon=%d selected=%d oldpos=%d,%d newpos=%d,%d\n",bool(editor()),QFileInfo::exists(path+"/Original.txt"),bool(renamed),renamed?renamed->isSelected():false,position.x(),position.y(),renamed?renamed->x():-1,renamed?renamed->y():-1);
            return 29;
        }
        source.close();
        // The user replaced the source while the editor was open: do not rename it.
        edit=start("Новое имя.txt");if(!edit)return 30;
        if(!QFile::rename(path+"/Новое имя.txt",path+"/../saved-original")||!write(path+"/Новое имя.txt","replacement\n"))return 31;
        edit->setPlainText("must-not-exist.txt");QTest::keyClick(edit,Qt::Key_Return);
        if(!editor()||QFileInfo::exists(path+"/must-not-exist.txt"))return 32;
        QFile replacement(path+"/Новое имя.txt");if(!replacement.open(QIODevice::ReadOnly)||replacement.readAll()!="replacement\n")return 33;
        QTest::keyClick(edit,Qt::Key_Escape);QTest::qWait(300);
        // Rename a directory with hidden contents, then a dangling symbolic link.
        if(!QDir().mkdir(path+"/Folder")||!write(path+"/Folder/.hidden","nested bytes")||!QFile::link(path+"/missing-target",path+"/offline-link"))return 34;
        QTest::qWait(600);edit=start("Folder");if(!edit)return 35;
        edit->setPlainText("Новая папка");QTest::keyClick(edit,Qt::Key_Return);QTest::qWait(600);
        QFile hidden(path+"/Новая папка/.hidden");if(!hidden.open(QIODevice::ReadOnly)||hidden.readAll()!="nested bytes")return 36;
        edit=start("offline-link");if(!edit)return 37;
        edit->setPlainText("renamed-link");QTest::keyClick(edit,Qt::Key_Return);QTest::qWait(600);
        if(!QFileInfo(path+"/renamed-link").isSymLink()||QFileInfo::exists(path+"/missing-target"))return 38;
        // Clicking outside the editor saves, without a modal prompt.
        if(!write(path+"/click-save.txt","click bytes"))return 45;
        QTest::qWait(600);edit=start("click-save.txt");if(!edit)return 46;
        edit->setPlainText("clicked.txt");scene.setFocus(Qt::MouseFocusReason);QTest::qWait(600);
        if(editor()||!QFileInfo::exists(path+"/clicked.txt")||QFileInfo::exists(path+"/click-save.txt"))return 47;
        // Context-menu rename continues to use the existing native dialog.
        bool modal=false;
        QTimer::singleShot(30,[&]{auto *dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());modal=dialog&&dialog->objectName()=="DesktopTextInput";if(dialog)dialog->reject();});
        auto *folder=layoutIcon(scene,"Новая папка");if(!folder)return 39;
        emit folder->renameRequested(folder);QTest::qWait(250);if(!modal||editor())return 40;
        // Switching page cancels an unfinished inline edit without renaming files.
        edit=start("renamed-link");if(!edit)return 41;edit->setPlainText("cancel-on-page-change");
        auto sets=DesktopLayout::instance()->pageSets();sets["DP-1"].append(DesktopPage{"other","Другая"});
        if(!DesktopLayout::instance()->replacePages(sets)||!scene.setCurrentPage("other"))return 42;
        QTest::keyClick(edit,Qt::Key_Return); // Page changed before its queued repaint.
        QTest::qWait(500);if(editor()||QFileInfo(path+"/cancel-on-page-change").isSymLink()||!QFileInfo(path+"/renamed-link").isSymLink())return 43;
        return 0;
    }();
    QWindowSystemInterface::handleScreenRemoved(screen);
    if(result)qCritical("FAIL inline rename check: %d",result);
    else qInfo("PASS inline rename: timing, double-click, drag, cancel, safe rename, conflicts, replacement, folders, links, dialog and pages");
    return result;
}
