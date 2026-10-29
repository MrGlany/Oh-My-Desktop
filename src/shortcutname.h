#pragma once
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QLocale>
#include <QHash>

inline QString shortcutBaseName(const QString &path)
{
    QHash<QString,QString> names;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        bool main = false;
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.startsWith('[')) { main = line == "[Desktop Entry]"; continue; }
            if (!main || line.startsWith('#')) continue;
            const int separator = line.indexOf('=');
            if (separator > 0) names.insert(line.left(separator).trimmed(), line.mid(separator+1).trimmed());
        }
    }
    const QString locale = QLocale::system().name();
    QString name = names.value("Name["+locale+"]",
        names.value("Name["+locale.section('_',0,0)+"]", names.value("Name")));
    if (name.isEmpty()) name = QFileInfo(path).completeBaseName();
    for (QChar &c : name) if (c == '/' || c == '\\' || !c.isPrint()) c = ' ';
    name = name.simplified();
    if (name == "." || name == ".." || name.isEmpty()) name = "Приложение";
    // Bound the filename by UTF-8 bytes, leaving room for collision suffixes.
    while (name.toUtf8().size() > 200) name.chop(1);
    return name;
}
