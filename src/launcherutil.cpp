#include "launcherutil.h"
#include <QProcess>

bool LauncherUtil::startDetachedWindowedCommand(const QString &command)
{
    if (command.trimmed().isEmpty()) return false;
    return QProcess::startDetached("/bin/sh", {"-c", command});
}

bool LauncherUtil::startDetachedWindowedProgram(const QString &program, const QStringList &arguments)
{
    if (program.trimmed().isEmpty()) return false;
    return QProcess::startDetached(program, arguments);
}

bool LauncherUtil::openLocation(const QString &uri)
{
    // Use Dolphin directly for virtual desktop locations; root shows local storage.
    if (uri.startsWith("computer:")) return startDetachedWindowedProgram("dolphin", {"/"});
    return startDetachedWindowedProgram(uri.startsWith("trash:") ? "dolphin" : "xdg-open", {uri});
}
