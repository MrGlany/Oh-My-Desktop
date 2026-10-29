#pragma once

#include <QString>
#include <QStringList>

namespace LauncherUtil {
bool openLocation(const QString &uri);

bool startDetachedWindowedCommand(const QString &command);
bool startDetachedWindowedProgram(const QString &program,
                                  const QStringList &arguments = {});

}
