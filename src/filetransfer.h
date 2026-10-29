#pragma once
#include <QString>
class QWidget;
namespace FileTransfer {
bool run(const QString &source, const QString &destination, bool move,
         bool replace, QWidget *parent = nullptr, QString *error = nullptr);
}
