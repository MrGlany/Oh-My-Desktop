#pragma once
#include <QString>

// Publish only this application's owned service menu. No extra process/polling.
class DolphinIntegration {
public:
    bool activate();
    void deactivate();
    ~DolphinIntegration(){deactivate();}
    static bool cleanupStopped();
private:
    static QString menuPath();
    QString m_ownedContent;
};
