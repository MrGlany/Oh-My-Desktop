#pragma once
#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
inline QPalette desktopThemePalette() {
    QPalette colors = QApplication::palette();
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/kdeglobals";
    QSettings settings(path, QSettings::IniFormat);
    auto read = [&](const QString &key, QColor fallback) {
        QStringList rgb = settings.value(key).toStringList();
        if (rgb.size() == 1) rgb = rgb.first().split(',');
        if (rgb.size() < 3) return fallback;
        bool a, b, c;
        int r=rgb[0].toInt(&a), g=rgb[1].toInt(&b), v=rgb[2].toInt(&c);
        return a && b && c ? QColor(r,g,v) : fallback;
    };
    colors.setColor(QPalette::Window, read("Colors:Window/BackgroundNormal", colors.window().color()));
    colors.setColor(QPalette::WindowText, read("Colors:Window/ForegroundNormal", colors.windowText().color()));
    colors.setColor(QPalette::Highlight, read("Colors:Selection/BackgroundNormal", colors.highlight().color()));
    colors.setColor(QPalette::HighlightedText, read("Colors:Selection/ForegroundNormal", colors.highlightedText().color()));
    colors.setColor(QPalette::Mid, read("Colors:Window/ForegroundInactive", colors.mid().color()));
    colors.setColor(QPalette::Base, read("Colors:View/BackgroundNormal", colors.base().color()));
    colors.setColor(QPalette::Text, read("Colors:View/ForegroundNormal", colors.text().color()));
    colors.setColor(QPalette::AlternateBase, read("Colors:View/BackgroundAlternate", colors.alternateBase().color()));
    colors.setColor(QPalette::Button, read("Colors:Button/BackgroundNormal", colors.button().color()));
    colors.setColor(QPalette::ButtonText, read("Colors:Button/ForegroundNormal", colors.buttonText().color()));
    return colors;
}
