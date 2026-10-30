#include "launcherutil.h"
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QUrl>
#include <QRegularExpression>

bool LauncherUtil::startDetachedWindowedCommand(const QString &command)
{
    if (command.trimmed().isEmpty()) return false;
    return startDetachedWindowedProgram("/bin/sh", {"-c", command});
}

bool LauncherUtil::startDetachedWindowedProgram(const QString &program, const QStringList &arguments,const QString &workingDirectory)
{
    if (program.trimmed().isEmpty()) return false;
    // setsid()/startDetached do not escape a systemd service's cgroup.
    // Give GUI apps an independent scope with the original session environment.
    const QString runner = QStandardPaths::findExecutable("systemd-run");
    if (!runner.isEmpty() && qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS")) {
        QStringList scoped{"--user", "--scope", "--collect", "--quiet",
                           "--slice=app.slice", "--expand-environment=no", "--", program};
        scoped.append(arguments);
        return QProcess::startDetached(runner, scoped,workingDirectory);
    }
    return QProcess::startDetached(program, arguments,workingDirectory);
}

bool LauncherUtil::openLocation(const QString &uri)
{
    // Use Dolphin directly for virtual desktop locations; root shows local storage.
    if (uri.startsWith("computer:")) return startDetachedWindowedProgram("dolphin", {"/"});
    return startDetachedWindowedProgram(uri.startsWith("trash:") ? "dolphin" : "xdg-open", {uri});
}

QString LauncherUtil::nativeExecutablePath(const QFileInfo &file)
{
    const QString path=file.canonicalFilePath();const QFileInfo target(path);
    if(path.isEmpty()||!target.isFile()||target.fileName().contains(QRegularExpression("\\.so(?:\\.[0-9]+)*$")))return {};
    QFile input(path);if(!input.open(QIODevice::ReadOnly))return {};
    const QByteArray header=input.read(64);
    if(header.size()<52||header.left(4)!=QByteArray("\x7f" "ELF",4))return {};
    const int format=quint8(header[4]),endian=quint8(header[5]);
    if((format!=1&&format!=2)||(endian!=1&&endian!=2)||quint8(header[6])!=1||(format==2&&header.size()<64))return {};
    auto number=[&](int offset,int length){quint64 value=0;for(int n=0;n<length;++n){const int index=endian==1?offset+length-1-n:offset+n;value=(value<<8)|quint8(header[index]);}return value;};
    const auto type=number(16,2),entry=number(24,format==2?8:4);
    // PIE applications and AppImage runtimes are ET_DYN, but ordinary shared
    // libraries have no entry point and must stay ordinary files.
    if((type!=2&&type!=3)||entry==0)return {};
    return path;
}

bool LauncherUtil::revealFile(const QFileInfo &file)
{
    const QString target=QFileInfo(file.absoluteFilePath()).canonicalFilePath();
    if(target.isEmpty())return false;
    // A file URL keeps spaces, #, Unicode and leading '-' literal. Dolphin's
    // --select opens the containing directory and highlights the target itself.
    return startDetachedWindowedProgram("dolphin",{"--select",QUrl::fromLocalFile(target).toString(QUrl::FullyEncoded)});
}
