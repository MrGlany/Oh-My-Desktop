#pragma once

#include <QDialog>
#include <QFileInfo>
#include <QStringList>

class QLineEdit;
class QLabel;

class FilePropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FilePropertiesDialog(const QFileInfo &info, QWidget *parent = nullptr);

private:
    void updateIconPreview();
    bool saveChanges();
    void loadDesktopEntryFields();
    bool saveDesktopEntryFields();

    QFileInfo m_info;
    QLabel *m_iconPreview=nullptr;
    QString m_customIcon,m_originalCustomIcon,m_fileIdentity;
    QByteArray m_originalEntry;
    QStringList m_originalFields;
    bool m_isDesktopEntry = false;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_execEdit = nullptr;
    QLineEdit *m_iconEdit = nullptr;
};
