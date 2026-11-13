#pragma once
#include "audio/AudioEngine.h"
#include "core/WindowEnumerator.h"
#include "utils/AppSettings.h"
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace sf {

// Decides which processes are captured and to which ear. Many apps per ear are allowed.
// Routing starts automatically with the first assignment and stops with the last one.
class StereoRouter : public QObject {
    Q_OBJECT
public:
    StereoRouter(AudioEngine* engine, AppSettings* settings, QObject* parent = nullptr);

    const QList<WindowInfo>& windows() const { return windows_; }
    const QList<Assignment>& assignments() const { return assignments_; }
    bool assignedChannel(const QString& key, Channel* ch) const;
    bool isRunning(const QString& key) const { return running_.contains(key); }
    int runningCount() const { return running_.size(); }
    // Ear capacity (kMaxAppsPerEar apps per ear).
    int appsOnEar(Channel c) const;
    bool earFull(Channel c) const { return appsOnEar(c) >= kMaxAppsPerEar; }
    bool canPlace(const QString& key, Channel ch) const;     // false when `ch` is full and `key` is not already on it
    float outputPeak(Channel c) const { return engine_->outputPeak(c); }

    void refreshWindows();                                   // re-scan open windows (Refresh button)
    void assign(const QString& key, const QString& name, const QString& exePath, Channel ch);
    void unassign(const QString& key);
    void setChannel(const QString& key, Channel ch);
    // Drag & drop of an .exe or .lnk: starts the app if it is not running, then routes it to `ch`.
    bool assignFile(const QString& path, Channel ch, QString* error);
    // Same for several dropped items; the first problem is reported in the footer. Used by every drop target.
    void assignFiles(const QStringList& paths, Channel ch);
    void swapSides();                                        // every Left app <-> Right, and vice versa

    int earVolume(Channel c) const { return volume_[static_cast<int>(c)]; }
    void setEarVolume(Channel c, int percent);               // 0..100

    void shutdown();                                         // stop routing, restore original sound

signals:
    void windowsChanged();
    void assignmentsChanged();
    void message(const QString& text);

private slots:
    void onSourceFailed(quint32 pid, const QString& reason);
    void tick();

private:
    int indexOf(const QString& key) const;
    void reconcile();
    void updateWindowList(bool forceSignal = false);      // open windows + placeholders for routed apps without a window
    void adoptLaunchedApp();                              // launcher shortcuts: follow the app that really appeared
    void removeAssignments(const QStringList& keys);
    void persist();
    void report(const QString& text);
    void reportEarFull(Channel c);
    float gainFor(Channel c) const { return volume_[static_cast<int>(c)] / 100.0f; }

    AudioEngine* engine_;
    AppSettings* settings_;
    QList<WindowInfo> windows_;
    QList<Assignment> assignments_;
    QSet<QString> running_;       // assigned apps that currently have a live process
    // Processes whose capture failed: retried with a growing delay (2 s, 4 s ... 15 s), never given up for good.
    struct Failure { int count = 0; qint64 retryAt = 0; };
    QHash<quint32, Failure> failedPids_;
    void markFailed(quint32 pid);
    bool inBackoff(quint32 pid) const;
    QHash<quint32, int> missing_;        // consecutive reconciles a routed pid was not found (anti-flapping)
    bool forcePrune_ = false;            // unassign: drop captures at once
    // Apps that were closed are removed from their ear after being absent twice in a row (not just dimmed).
    // A freshly dropped / launched app and apps restored at start-up get a grace period to appear first.
    QHash<QString, int> absent_;
    QHash<QString, qint64> graceUntil_;
    struct PendingLaunch { QString key; QSet<QString> windowsBefore; qint64 since = 0, deadline = 0; };
    QList<PendingLaunch> pending_;
    QTimer timer_;
    QString lastMessage_;
    int volume_[2] = {100, 100};
};

} // namespace sf
