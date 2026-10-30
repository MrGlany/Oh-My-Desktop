#pragma once
#include <QWheelEvent>

// React to the first delivered wheel update, including pixel-only updates.
// Positive (wheel up) moves right; a phase-only event does not change pages.
inline int desktopWheelStep(const QWheelEvent *event) {
    const int angle=event->angleDelta().y();
    const int delta=angle ? angle : event->pixelDelta().y();
    return delta>0 ? 1 : delta<0 ? -1 : 0;
}
