#pragma once
#include <QHash>
class QScreen;
class DesktopWindow;
void scheduleSmokeTest(const QHash<QScreen *, DesktopWindow *> &windows);
