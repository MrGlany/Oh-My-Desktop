#pragma once

#include <QFileInfo>
#include <QPixmap>

// Compatibility wrapper for the asynchronous application icon resolver.
// Windows PE extraction uses optional wrestool; no application is executed.
// Returns a null pixmap while discovery is pending or no resource is available.
namespace ExeIconUtil {

QPixmap loadEmbeddedIcon(const QFileInfo &fileInfo);

}
