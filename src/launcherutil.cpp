#include "launcherutil.h"
#include <QProcess>
#include <QStandardPaths>

bool LauncherUtil::startDetachedWindowedCommand(const QString &command)
{
    if (command.trimmed().isEmpty()) return false;
    return startDetachedWindowedProgram("/bin/sh", {"-c", command});
}

bool LauncherUtil::startDetachedWindowedProgram(const QString &program, const QStringList &arguments)
{
    if (program.trimmed().isEmpty()) return false;
    // setsid()/startDetached do not escape a systemd service's cgroup.
    // Give GUI apps an independent scope with the original session environment.
    const QString runner = QStandardPaths::findExecutable("systemd-run");
    if (!runner.isEmpty() && qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS")) {
        QStringList scoped{"--user", "--scope", "--collect", "--quiet",
                           "--slice=app.slice", "--expand-environment=no", "--", program};
        scoped.append(arguments);
        return QProcess::startDetached(runner, scoped);
    }
    return QProcess::startDetached(program, arguments);
}

bool LauncherUtil::openLocation(const QString &uri)
{
    // Use Dolphin directly for virtual desktop locations; root shows local storage.
    if (uri.startsWith("computer:")) return startDetachedWindowedProgram("dolphin", {"/"});
    return startDetachedWindowedProgram(uri.startsWith("trash:") ? "dolphin" : "xdg-open", {uri});
}
