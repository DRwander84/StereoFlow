#pragma once
#include "routing/ChannelMapping.h"
#include <QList>
#include <QPoint>
#include <QSettings>
#include <QString>
#include <QVariant>

namespace sf {

struct Assignment {
    QString key;       // lower-case executable path (stable across PIDs)
    QString name;
    QString exePath;
    Channel channel = Channel::Left;
};

// Typed wrapper over QSettings (INI file in %APPDATA%, no registry use).
class AppSettings {
public:
    AppSettings();

    int earVolume(Channel c) const;                 // 0..100
    void setEarVolume(Channel c, int percent);

    // Hidden option (edit the INI): set audio/suppressOriginal=false if the routed sound
    // goes silent when the original app is muted (see docs/Troubleshooting.md).
    bool suppressOriginal() const;

    QList<Assignment> assignments() const;
    void setAssignments(const QList<Assignment>& list);

    QVariant windowPos() const;                     // QPoint or invalid
    void setWindowPos(const QPoint& p);

private:
    mutable QSettings s_;
};

} // namespace sf
