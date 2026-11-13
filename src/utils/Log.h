#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace sf {

// Tiny event log (a few lines per user action, never per audio block): %LOCALAPPDATA%\StereoFlow\stereoflow.log
inline void logLine(const QString& text) {
    static const QString path = [] {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dir);
        const QString p = QDir(dir).filePath("stereoflow.log");
        QFile f(p);
        if (f.exists() && f.size() > 512 * 1024) f.remove();
        return p;
    }();
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "  " << text << '\n';
}

} // namespace sf
