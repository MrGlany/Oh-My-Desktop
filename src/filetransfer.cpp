#include "filetransfer.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>

bool FileTransfer::run(const QString &source, const QString &destination,
                       bool move, bool replace, QWidget *parent, QString *error)
{
    QProcess process;
    QStringList args{QDir(QCoreApplication::applicationDirPath()).filePath("safe_transfer.py")};
    if (move) args << "--move";
    if (replace) args << "--replace";
    args << "--" << source << destination;
    process.start("/usr/bin/python3", args);
    // Never kill a transaction in the middle of a commit. Transfers are synchronous
    // in this experimental build; large transfers should use Dolphin instead.
    const bool finished = process.waitForStarted() && process.waitForFinished(-1);
    const auto result = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || !result.value("ok").toBool()) {
        const QString message = result.value("error").toString(
            QString::fromUtf8(process.readAllStandardError()).left(2000));
        if (error) *error = message;
        else QMessageBox::warning(parent, "Transfer failed — originals retained", message);
        return false;
    }
    QStringList recovery;
    for (const auto &value : result.value("recovery").toArray()) recovery << value.toString();
    if (!recovery.isEmpty())
        QMessageBox::information(parent, "Recovery copies retained",
            "For safety, no originals were deleted. Recovery locations:\n" + recovery.join('\n'));
    return true;
}
