#include "desktopscene.h"
#include "desktopicon.h"
#include "desktopconstants.h"
#include "filetransfer.h"

struct Collision { QString path; bool replace = false; };

#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>

// External drop (file manager to desktop)

static Collision resolveCollision(
    const QString &desktopPath,
    const QString &srcFileName,
    const QString &srcPath,
    QSet<QString> &alreadyUsed,
    QWidget *parent)
{
    const QString candidate = desktopPath + "/" + srcFileName;

    if (!QFileInfo::exists(candidate) && !alreadyUsed.contains(srcFileName)) {
        alreadyUsed.insert(srcFileName);
        return {candidate, false};
    }

    const QFileInfo srcInfo(srcPath);
    if (srcInfo.exists() && srcInfo.absoluteFilePath() == candidate)
        return {};

    const auto btn = QMessageBox::question(
        parent,
        "File Already Exists",
        QString("An item named '%1' already exists on the Desktop.\n\n"
                "Do you want to replace it?").arg(srcFileName),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (btn == QMessageBox::Cancel)
        return {};
    if (btn == QMessageBox::Yes) {
        alreadyUsed.insert(srcFileName);
        return {candidate, true};
    }

    const QFileInfo srcFileEntry(desktopPath + "/" + srcFileName);
    const QString baseName = srcFileEntry.completeBaseName();
    const QString suffix = srcFileEntry.suffix().isEmpty() ? "" : "." + srcFileEntry.suffix();
    int counter = 1;
    QString newName;
    do {
        newName = baseName + " (" + QString::number(counter) + ")" + suffix;
        ++counter;
    } while (QFileInfo::exists(desktopPath + "/" + newName)
             || alreadyUsed.contains(newName));

    alreadyUsed.insert(newName);
    return {desktopPath + "/" + newName, false};
}

void DesktopScene::dragEnterEvent(QDragEnterEvent *event)
{
    if (qobject_cast<DesktopScene *>(event->source()) && event->mimeData()->hasFormat("application/x-oh-my-desktop-icons")) {
        event->setDropAction(Qt::MoveAction);event->accept();return;
    }
    if (event->mimeData()->hasUrls()) {
        bool hasLocal = false;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                hasLocal = true;
                break;
            }
        }
        if (hasLocal) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void DesktopScene::dragMoveEvent(QDragMoveEvent *event)
{
    if (qobject_cast<DesktopScene *>(event->source()) && event->mimeData()->hasFormat("application/x-oh-my-desktop-icons")) {
        event->setDropAction(Qt::MoveAction);event->accept();return;
    }
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DesktopScene::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-oh-my-desktop-icons")) {
        auto *source = qobject_cast<DesktopScene *>(event->source());
        if (!source) {event->ignore();return;}
        QStringList keys;
        for (const auto &key : QJsonDocument::fromJson(event->mimeData()->data("application/x-oh-my-desktop-icons")).array()) keys.append(key.toString());
        if (receiveDesktopDrop(source, keys, event->position().toPoint())) {
            event->setDropAction(Qt::MoveAction);event->accept();
        } else event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        event->ignore();
        return;
    }

    const bool preferMove = event->proposedAction() == Qt::MoveAction;
    const QPoint dropPos = event->position().toPoint();
    QPair<int,int> startCell = posToCell(dropPos);

    int transferred = 0;
    QStringList errors;
    QSet<QString> usedNames;

    for (const QUrl &url : urls) {
        if (!url.isLocalFile())
            continue;

        const QString srcPath = url.toLocalFile();
        const QFileInfo srcInfo(srcPath);
        if (!srcInfo.exists())
            continue;

        const auto resolved = resolveCollision(m_desktopPath, srcInfo.fileName(), srcPath, usedNames, this);
        if (resolved.path.isEmpty()) {
            errors << QString("Skipped '%1'.").arg(srcInfo.fileName());
            continue;
        }

        const QString finalDest = resolved.path;
        QString error;
        const bool ok = FileTransfer::run(srcPath, finalDest, preferMove, resolved.replace, this, &error);

        if (!ok) {
            errors << QString("Could not transfer '%1': %2").arg(srcInfo.fileName(), error);
            continue;
        }

        const QFileInfo destInfo(finalDest);
        claimEntry(destInfo.fileName());

        for (int i = 0; i < m_icons.size(); ++i) {
            if (!m_icons[i]->isVirtual()
                && m_icons[i]->fileInfo().fileName() == destInfo.fileName()) {
                m_icons[i]->deleteLater();
                m_icons.removeAt(i);
                --i;
            }
        }

        const auto cell = nearestFreeCell(startCell, destInfo.fileName());
        m_occupiedCells[cell] = destInfo.fileName();
        const QPoint snapped = cellToPos(cell.first, cell.second);
        m_positions[destInfo.fileName()] = snapped;

        auto *icon = new DesktopIcon(destInfo, m_iconSize, m_showPreview, this);
        icon->move(snapped);
        icon->show();
        connectIconSignals(icon, false);
        m_icons.append(icon);

        if (++startCell.second >= maxRows()) { startCell.second = 0; ++startCell.first; }
        ++transferred;
    }

    if (transferred > 0)
        queueSavePositions(kSaveDebounceMs);

    if (!errors.isEmpty()) {
        QMessageBox::warning(this, "Transfer incomplete", errors.join('\n'));
        event->setDropAction(Qt::IgnoreAction);
    } else {
        event->acceptProposedAction();
    }
}
