#include "desktopscene.h"
#include "desktopicon.h"
#include "desktopconstants.h"
#include "filetransfer.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSet>
#include <QUrl>

// ── Clipboard helpers ────────────────────────────────────────────────────────

struct Collision { QString path; bool replace = false; };

// Remove the old icon from the scene and return its position (so the new icon
// can be placed in the same spot). Returns null point if no match found.
static QPoint removeOldIcon(QList<DesktopIcon *> &icons, const QString &fileName)
{
    QPoint oldPos;
    for (int i = 0; i < icons.size(); ++i) {
        DesktopIcon *icon = icons[i];
        if (!icon->isVirtual() && icon->fileInfo().fileName() == fileName) {
            oldPos = icon->pos();
            icon->deleteLater();
            icons.removeAt(i);
            --i;
        }
    }
    return oldPos;
}

// ── Clipboard (copy / cut / paste) ─────────────────────────────────────────

void DesktopScene::copySelectedIcons()
{
    m_clipboardCutMode = false;
    m_clipboardPaths.clear();

    for (auto *icon : m_icons) {
        if (icon->isSelected() && !icon->isVirtual())
            m_clipboardPaths.append(icon->fileInfo().absoluteFilePath());
    }
    if (m_clipboardPaths.isEmpty()) return;

    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : m_clipboardPaths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    QByteArray content = "copy\n";
    for (const QString &path : m_clipboardPaths)
        content += QUrl::fromLocalFile(path).toEncoded() + "\n";
    mime->setData("x-special/gnome-copied-files", content);

    QApplication::clipboard()->setMimeData(mime);
    updateCutIconAppearance();
}

void DesktopScene::cutSelectedIcons()
{
    m_clipboardCutMode = true;
    m_clipboardPaths.clear();

    for (auto *icon : m_icons) {
        if (icon->isSelected() && !icon->isVirtual())
            m_clipboardPaths.append(icon->fileInfo().absoluteFilePath());
    }
    if (m_clipboardPaths.isEmpty()) return;

    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &path : m_clipboardPaths)
        urls.append(QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    QByteArray content = "cut\n";
    for (const QString &path : m_clipboardPaths)
        content += QUrl::fromLocalFile(path).toEncoded() + "\n";
    mime->setData("x-special/gnome-copied-files", content);

    QByteArray kdeCut("1");
    mime->setData("application/x-kde-cutselection", kdeCut);

    QApplication::clipboard()->setMimeData(mime);
    updateCutIconAppearance();
}

void DesktopScene::updateCutIconAppearance()
{
    QSet<QString> cutPaths;
    if (m_clipboardCutMode) {
        for (const QString &path : m_clipboardPaths)
            cutPaths.insert(path);
    }

    for (auto *icon : m_icons) {
        if (icon->isVirtual()) continue;
        icon->setCutState(cutPaths.contains(icon->fileInfo().absoluteFilePath()));
    }
}

// Resolve naming collision on the desktop.
// Returns the final destination path, or empty string if the user chose to skip.
// If srcPath points to the same file as the candidate, generates a unique name
// instead of offering replace (self-paste case).
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

    // Check if source and destination are the same file (self-paste)
    const QFileInfo srcInfo(srcPath);
    if (srcInfo.exists() && srcInfo.absoluteFilePath() == candidate) {
        // Self-paste: just generate a unique name, don't offer replace
    } else {
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
        // btn == No, fall through to unique name generation
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

void DesktopScene::pasteFromClipboard()
{
    // Figure out whether to use internal clipboard or system clipboard.
    // If the system clipboard has different paths, the user copied from another
    // app (e.g. Dolphin) - clear our stale internal state and use system.
    bool useInternal = !m_clipboardPaths.isEmpty();
    if (useInternal) {
        const auto *clip = QApplication::clipboard();
        const auto *mime = clip->mimeData();
        bool match = false;
        if (mime && mime->hasUrls()) {
            QStringList sysPaths;
            for (const QUrl &url : mime->urls())
                if (url.isLocalFile())
                    sysPaths.append(url.toLocalFile());
            match = (sysPaths == m_clipboardPaths);
        }
        if (!match) {
            useInternal = false;
            m_clipboardPaths.clear();
            m_clipboardCutMode = false;
            updateCutIconAppearance();
        }
    }

    if (!useInternal) {
        // External paste from system clipboard
        const auto *clip = QApplication::clipboard();
        const auto *mime = clip->mimeData();
        if (!mime || !mime->hasUrls()) return;

        QStringList paths;
        for (const QUrl &url : mime->urls())
            if (url.isLocalFile())
                paths.append(url.toLocalFile());
        if (paths.isEmpty()) return;

        // Detect cut vs copy from clipboard format
        bool doCut = false;
        if (mime->hasFormat("x-special/gnome-copied-files")) {
            doCut = mime->data("x-special/gnome-copied-files").startsWith("cut\n");
        } else if (mime->hasFormat("application/x-kde-cutselection")) {
            doCut = mime->data("application/x-kde-cutselection") == "1";
        }

        QSet<QString> usedNames;
        for (const QString &srcPath : paths) {
            const QFileInfo srcInfo(srcPath);
            if (!srcInfo.exists()) continue;

            const auto resolved = resolveCollision(m_desktopPath, srcInfo.fileName(), srcPath, usedNames, this);
            if (resolved.path.isEmpty()) continue;

            const QString finalDest = resolved.path;
            bool ok = FileTransfer::run(srcPath, finalDest, doCut, resolved.replace, this);
            if (!ok) continue;

            const QFileInfo destInfo(finalDest);
            claimEntry(destInfo.fileName());
            QPoint oldPos = removeOldIcon(m_icons, destInfo.fileName());

            const auto cell = nearestFreeCell(posToCell(m_lastContextMenuPos), destInfo.fileName());
            m_occupiedCells[cell] = destInfo.fileName();
            const QPoint snapped = oldPos.isNull()
                ? cellToPos(cell.first, cell.second) : oldPos;
            m_positions[destInfo.fileName()] = snapped;

            auto *icon = new DesktopIcon(destInfo, m_iconSize, m_showPreview, this);
            icon->move(snapped);
            icon->show();
            connectIconSignals(icon, false);
            m_icons.append(icon);
        }

        // Clear clipboard after paste so Paste menu item disappears
        m_clipboardPaths.clear();
        m_clipboardCutMode = false;
        // Keep the system clipboard for retry, including failed transfers.
        updateCutIconAppearance();

        queueSavePositions(kSaveDebounceMs);
        queueRefresh(50);
        return;
    }

    // Internal paste
    const QPoint pastePos = m_lastContextMenuPos.isNull()
        ? QPoint(width() / 2, height() / 2) : m_lastContextMenuPos;
    QPair<int,int> startCell = posToCell(pastePos);

    QSet<QString> usedNames;
    for (const QString &srcPath : m_clipboardPaths) {
        const QFileInfo srcInfo(srcPath);
        if (!srcInfo.exists()) continue;

        const auto resolved = resolveCollision(m_desktopPath, srcInfo.fileName(), srcPath, usedNames, this);
        if (resolved.path.isEmpty()) continue;

        const QString finalDest = resolved.path;
        bool ok = FileTransfer::run(srcPath, finalDest, m_clipboardCutMode, resolved.replace, this);
        if (!ok) continue;

        const QFileInfo destInfo(finalDest);
        claimEntry(destInfo.fileName());
        QPoint oldPos = removeOldIcon(m_icons, destInfo.fileName());

        const auto cell = nearestFreeCell(startCell, destInfo.fileName());
        m_occupiedCells[cell] = destInfo.fileName();
        const QPoint snapped = oldPos.isNull()
            ? cellToPos(cell.first, cell.second) : oldPos;
        m_positions[destInfo.fileName()] = snapped;

        auto *icon = new DesktopIcon(destInfo, m_iconSize, m_showPreview, this);
        icon->move(snapped);
        icon->show();
        connectIconSignals(icon, false);
        m_icons.append(icon);

        if (++startCell.second >= maxRows()) { startCell.second = 0; ++startCell.first; }
    }

    // Clear clipboard after paste
    m_clipboardPaths.clear();
    m_clipboardCutMode = false;
    // Keep the system clipboard for retry, including failed transfers.
    updateCutIconAppearance();

    queueSavePositions(kSaveDebounceMs);
    queueRefresh(50);
}
