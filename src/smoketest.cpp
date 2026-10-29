#include "smoketest.h"
#include "desktopwindow.h"
#include "desktopscene.h"
#include "desktopicon.h"
#include "inlinemenu.h"
#include <QApplication>
#include <QTest>
#include <QTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QDateTime>
#include <QLoggingCategory>

static void input(const QString &text) {
    QTimer::singleShot(150, [text] {
        auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
        if (!dialog) { qCritical() << "SMOKE missing input dialog"; return; }
        dialog->setTextValue(text);
        dialog->accept();
    });
}

void scheduleSmokeTest(const QHash<QScreen *, DesktopWindow *> &windows)
{
    if (qEnvironmentVariable("OMD_SMOKE_DRAG") == "1") {
        QTimer::singleShot(2000, [windows] {
            const QString root = qEnvironmentVariable("OMD_DESKTOP_DIR");
            if (root.isEmpty() || !QFile::exists(root+"/.omd-test-sentinel")) return;
            bool ok = windows.size() == 2;
            for (auto *window : windows) {
                auto *scene = window->findChild<DesktopScene *>();
                DesktopIcon *folder = nullptr;
                for (auto *icon : scene->findChildren<DesktopIcon *>())
                    if (!icon->isVirtual() && icon->fileInfo().fileName() == "Initial-Folder") folder = icon;
                if (!folder) { ok=false; continue; }
                const QPoint old = folder->pos();
                const QPoint local = folder->rect().center();
                const QPoint global = folder->mapToGlobal(local);
                QTest::mousePress(folder, Qt::LeftButton, Qt::NoModifier, local);
                QMouseEvent move(QEvent::MouseMove, local+QPoint(250,250), global+QPoint(250,250),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(folder, &move);
                QTest::mouseRelease(folder, Qt::LeftButton, Qt::NoModifier, local);
                ok &= folder->pos() != old;
                ok &= QFileInfo(root+"/Initial-Folder").isDir();
            }
            QFile file(root+"/drag-result.txt");
            if (file.open(QIODevice::WriteOnly)) file.write(ok ? "PASS\n" : "FAIL\n");
        });
    }
    if (qEnvironmentVariable("OMD_SMOKE_TEST") != "1") return;
    QLoggingCategory::setFilterRules("*.info=true\n*.warning=true\n*.critical=true");
    QTimer::singleShot(2500, [windows] {
        const QString root = qEnvironmentVariable("OMD_DESKTOP_DIR");
        if (root.isEmpty() || !QFile::exists(root + "/.omd-test-sentinel")) {
            qCritical() << "SMOKE refused: no isolated test sentinel"; qApp->exit(10); return;
        }
        bool ok = windows.size() == 2;
        int index=0;
        const auto runId = QDateTime::currentMSecsSinceEpoch();
        for (auto *window : windows) {
            auto *scene = window->findChild<DesktopScene *>();
            if (!scene) { ok=false; continue; }
            QTest::mouseClick(scene, Qt::RightButton, Qt::NoModifier, QPoint(500,300));
            bool menuVisible=false;
            for (auto *menu : scene->findChildren<InlineMenu *>()) menuVisible |= menu->isVisible();
            qInfo() << "SMOKE context menu" << window->screen()->name() << menuVisible;
            ok &= menuVisible;
            QMetaObject::invokeMethod(scene, "closeAllMenus", Qt::DirectConnection);
            QString name = QString("Smoke Folder %1-%2").arg(runId).arg(++index);
            input(name);
            ok &= QMetaObject::invokeMethod(scene, "onNewFolder", Qt::DirectConnection);
            QTest::qWait(700);
            DesktopIcon *folder=nullptr;
            for (auto *icon : scene->findChildren<DesktopIcon *>())
                if (!icon->isVirtual() && icon->fileInfo().fileName()==name) folder=icon;
            ok &= folder && QFileInfo(root+"/"+name).isDir();
            if (folder) {
                input(name+" renamed");
                QMetaObject::invokeMethod(scene, "onRenameRequested", Qt::DirectConnection,
                                          Q_ARG(DesktopIcon *, folder));
                QTest::qWait(700);
                ok &= QFileInfo(root+"/"+name+" renamed").isDir();
            }
            input(QString("Smoke Document %1-%2").arg(runId).arg(index));
            QMetaObject::invokeMethod(scene, "onNewDocument", Qt::DirectConnection);
            QTest::qWait(700);
            ok &= QFileInfo(root+QString("/Smoke Document %1-%2.txt").arg(runId).arg(index)).isFile();
            for (auto *icon : scene->findChildren<DesktopIcon *>()) {
                if (!icon->isVirtual() && icon->fileInfo().fileName()==name+" renamed") {
                    // Exercise the actual double-click handler on the isolated folder.
                    QTest::mouseDClick(icon,Qt::LeftButton,Qt::NoModifier,icon->rect().center());
                    qInfo() << "SMOKE double click" << icon->fileInfo().absoluteFilePath();
                    break;
                }
            }
            qInfo() << "SMOKE monitor" << window->screen()->name() << window->size() << "ok so far" << ok;
        }
        qInfo() << "SMOKE RESULT" << ok;
        QFile result(root+"/smoke-result.txt");
        if (result.open(QIODevice::WriteOnly)) result.write(ok ? "PASS\n" : "FAIL\n");
    });
}
