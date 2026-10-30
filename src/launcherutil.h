#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>

namespace LauncherUtil {
bool openLocation(const QString &uri);
bool revealFile(const QFileInfo &file);
QString nativeExecutablePath(const QFileInfo &file);

bool startDetachedWindowedCommand(const QString &command);
bool startDetachedWindowedProgram(const QString &program,
                                  const QStringList &arguments = {},
                                  const QString &workingDirectory = QString());

}
