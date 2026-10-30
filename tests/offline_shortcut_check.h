#pragma once
#include <sys/stat.h>

static int offlineShortcutCheck(bool reload)
{
    const QString path = qEnvironmentVariable("OMD_DESKTOP_DIR");
    if (!path.contains("properties-check")) return 150;
    QDir().mkpath(path);
    const QString target = path+"/../disk/Folder";
    const QString link = path+"/Offline folder";
    if (!reload) {
        if (!QFile::link(target, link)) return 151;
        if (!QFile::link(target, path+"/.Hidden offline")) return 152;
        if (::mkfifo(QFile::encodeName(path+"/fifo").constData(),0600)) return 153;
    }
    auto *l = new LayoutTestScreen("HDMI-A-1",0);
    auto *r = new LayoutTestScreen("DP-1",1920);
    QWindowSystemInterface::handleScreenAdded(l);
    QWindowSystemInterface::handleScreenAdded(r);
    const int result = [&]() -> int {
        DesktopScene left(l->screen()), right(r->screen());
        left.resize(1920,1080);right.resize(1920,1080);left.show();right.show();
        QTest::qWait(650);
        auto *icon = layoutIcon(reload ? left : right,"Offline folder");
        if (!icon || !icon->unavailableLink()) return 154;
        if (layoutIcon(reload ? right : left,"Offline folder")) return 155;
        if (layoutIcon(left,"fifo") || layoutIcon(right,"fifo")) return 156;
        if (layoutIcon(left,".Hidden offline") || layoutIcon(right,".Hidden offline")) return 157;
        QSettings expected(path+"/../offline-expected.ini",QSettings::IniFormat);
        if (reload) {
            if (icon->pos()!=expected.value("position").toPoint()) return 158;
            qInfo("PASS unavailable link restores position and monitor after separate-process restart");
            return 0;
        }
        if (!left.receiveDesktopDrop(&right,{"Offline folder"},QPoint(700,400))) return 159;
        QTest::qWait(650);
        icon = layoutIcon(left,"Offline folder");
        if (!icon || layoutIcon(right,"Offline folder")) return 160;
        const QPoint position = icon->pos();
        expected.setValue("position",position);expected.sync();
        QDir().mkpath(target);
        // Nothing in Desktop changed: the timer must detect an external mount.
        QTest::qWait(4200);
        if (icon->unavailableLink() || icon->pos()!=position) return 161;
        QDir().rmdir(target);
        QTest::qWait(4200);
        if (!icon->unavailableLink() || icon->pos()!=position || layoutIcon(right,"Offline folder")) return 162;
        icon->grab().save(path+"/../locked-icon.png");
        auto settings=left.desktopSettings();settings.showHidden=true;
        left.applyDesktopSettings(settings,false);
        right.applyDesktopSettings(settings,false);QTest::qWait(650);
        if (!layoutIcon(left,".Hidden offline") && !layoutIcon(right,".Hidden offline")) return 163;
        if (!QFileInfo(link).isSymLink() || QFileInfo(link).symLinkTarget()!=QDir::cleanPath(target)) return 164;
        qInfo("PASS dangling links visible once, hidden toggle, special-file exclusion, two-screen transfer, lock refresh on target appearance/disappearance, position and symlink preserved");
        return 0;
    }();
    QWindowSystemInterface::handleScreenRemoved(l);
    QWindowSystemInterface::handleScreenRemoved(r);
    return result;
}
