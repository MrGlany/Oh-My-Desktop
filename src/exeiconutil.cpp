#include "exeiconutil.h"
#include "applicationicons.h"

// Compatibility entry point. Extraction now runs in the bounded asynchronous
// application icon resolver, with private temporary files and no UI blocking.
QPixmap ExeIconUtil::loadEmbeddedIcon(const QFileInfo &fileInfo)
{
    if(fileInfo.suffix().compare("exe",Qt::CaseInsensitive)!=0)return {};
    const auto icon=ApplicationIcons::instance()->icon(fileInfo);
    return icon.isNull()?QPixmap():icon.pixmap(256,256);
}
