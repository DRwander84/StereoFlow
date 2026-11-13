#include "routing/StereoRouter.h"
#include "core/ProcessManager.h"
#include "core/ShortcutResolver.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QProcess>
#include <QStringList>
#include <QUrl>
#include <map>
#include <set>
#include <windows.h>
#include <shellapi.h>

namespace sf {

StereoRouter::StereoRouter(AudioEngine* e, AppSettings* st, QObject* parent)
    : QObject(parent), engine_(e), settings_(st) {
    assignments_ = settings_->assignments();
    volume_[0] = settings_->earVolume(Channel::Left);
    volume_[1] = settings_->earVolume(Channel::Right);
    engine_->setSuppressOriginal(settings_->suppressOriginal());

    connect(engine_, &AudioEngine::sourceFailed, this, &StereoRouter::onSourceFailed);
    connect(engine_, &AudioEngine::problem, this, &StereoRouter::report);

    // New processes / new audio sessions (e.g. a browser starting playback) are picked up here.
    timer_.setInterval(2000);
    connect(&timer_, &QTimer::timeout, this, &StereoRouter::tick);
    timer_.start();
    QTimer::singleShot(200, this, &StereoRouter::reconcile);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto& a : assignments_) graceUntil_[a.key] = now + 6000;   // restored from settings: give them time
    refreshWindows();
}

int StereoRouter::indexOf(const QString& key) const {
    for (int i = 0; i < assignments_.size(); ++i) if (assignments_[i].key == key) return i;
    return -1;
}

bool StereoRouter::assignedChannel(const QString& key, Channel* ch) const {
    const int i = indexOf(key);
    if (i < 0) return false;
    if (ch) *ch = assignments_[i].channel;
    return true;
}

int StereoRouter::appsOnEar(Channel c) const {
    int n = 0;
    for (const auto& a : assignments_) n += (a.channel == c);
    return n;
}

bool StereoRouter::canPlace(const QString& key, Channel ch) const {
    const int i = indexOf(key);
    const bool already = i >= 0 && assignments_[i].channel == ch;
    return earHasRoom(appsOnEar(ch), already);
}

void StereoRouter::reportEarFull(Channel c) {
    lastMessage_.clear();                                   // the same warning must show again on the next attempt
    report(QString("%1 ear is full (%2 apps max). Remove one first.")
               .arg(c == Channel::Left ? "Left" : "Right").arg(kMaxAppsPerEar));
}

void StereoRouter::report(const QString& text) {
    if (text == lastMessage_) return;
    lastMessage_ = text;
    emit message(text);
}

void StereoRouter::persist() { settings_->setAssignments(assignments_); }

void StereoRouter::markFailed(quint32 pid) {
    Failure& f = failedPids_[pid];
    ++f.count;
    const qint64 delay = qMin<qint64>(2000LL << qMin(f.count - 1, 3), 15000);
    f.retryAt = QDateTime::currentMSecsSinceEpoch() + delay;
}

bool StereoRouter::inBackoff(quint32 pid) const {
    auto it = failedPids_.constFind(pid);
    return it != failedPids_.constEnd() && QDateTime::currentMSecsSinceEpoch() < it->retryAt;
}

void StereoRouter::updateWindowList(bool forceSignal) {
    QList<WindowInfo> list = enumerateAppWindows(static_cast<quint32>(GetCurrentProcessId()));
    QSet<QString> have;
    for (const auto& w : list) have.insert(w.key);
    // A routed app that has no window yet (just launched) or never has one (tray app) still gets a row, so it can be
    // seen and changed in Applications right away.
    for (const auto& a : assignments_) {
        if (have.contains(a.key)) continue;
        WindowInfo p;
        p.key = a.key; p.appName = a.name; p.exePath = a.exePath;
        p.title = running_.contains(a.key) ? QStringLiteral("Running without a window") : QStringLiteral("Starting...");
        list.append(p);
    }
    bool changed = forceSignal || list.size() != windows_.size();
    for (int i = 0; !changed && i < list.size(); ++i)
        changed = list[i].key != windows_[i].key || list[i].title != windows_[i].title;
    windows_ = std::move(list);
    if (changed) emit windowsChanged();
}

void StereoRouter::refreshWindows() {
    failedPids_.clear();    // give apps that failed earlier another chance
    updateWindowList(true);
    reconcile();
}

void StereoRouter::tick() {
    bool scan = !pending_.isEmpty();
    for (const auto& a : assignments_) if (!running_.contains(a.key)) { scan = true; break; }
    if (scan) updateWindowList();
    reconcile();
}

void StereoRouter::assign(const QString& key, const QString& name, const QString& exePath, Channel ch) {
    if (!canPlace(key, ch)) { reportEarFull(ch); return; }
    const int i = indexOf(key);
    if (i >= 0) {
        assignments_[i].channel = ch;
    } else {
        Assignment a;
        a.key = key; a.name = name; a.exePath = exePath; a.channel = ch;
        assignments_.append(a);
        graceUntil_[key] = QDateTime::currentMSecsSinceEpoch() + 5000;
    }
    absent_.remove(key);
    persist();
    emit assignmentsChanged();
    reconcile();
}

bool StereoRouter::assignFile(const QString& path, Channel ch, QString* error) {
    const LaunchTarget target = resolveLaunchTarget(path);
    if (target.exePath.isEmpty()) {
        if (error) *error = target.error;
        return false;
    }
    if (!canPlace(target.exePath.toLower(), ch)) {          // full ear: do not even start the app
        if (error) *error = QString("%1 ear is full (%2 apps max). Remove one first.")
                                .arg(ch == Channel::Left ? "Left" : "Right").arg(kMaxAppsPerEar);
        return false;
    }
    const QFileInfo exe(target.exePath);
    const std::wstring wide = target.exePath.toStdWString();
    const bool alreadyRunning = !ProcessManager::pidsForExe(wide).empty();

    QSet<QString> before;                                    // windows open right now: to recognise the new one
    for (const auto& w : windows_) before.insert(w.key);
    assign(target.exePath.toLower(), ProcessManager::friendlyName(wide), target.exePath, ch);
    if (alreadyRunning) { updateWindowList(); return true; }  // open already: just route it, show it in Applications

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString key = target.exePath.toLower();
    bool started = false;
    if (target.isShortcut) {
        const std::wstring lnk = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
        started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", lnk.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    } else {
        started = QProcess::startDetached(target.exePath, {}, exe.absolutePath());
    }
    if (!started) {
        if (error) *error = exe.fileName() + " could not be started (administrator rights needed?)";
        // Not started: do not leave a dead tile behind.
        QMetaObject::invokeMethod(this, [this, key] { removeAssignments({key}); }, Qt::QueuedConnection);
        return false;
    }
    graceUntil_[key] = now + 30000;                          // slow starters (games, launchers) get 30 s
    PendingLaunch pl;
    pl.key = key; pl.windowsBefore = before; pl.since = now; pl.deadline = now + 25000;
    pending_.append(pl);
    updateWindowList();                                      // the row with its ear appears in Applications immediately
    // Pick the new process / window up quickly instead of waiting for the 2 s tick.
    for (int ms : {500, 1200, 2500, 5000, 9000})
        QTimer::singleShot(ms, this, [this] { updateWindowList(); reconcile(); });
    return true;
}

void StereoRouter::assignFiles(const QStringList& paths, Channel ch) {
    QString firstError;
    for (const QString& p : paths) {
        QString err;
        if (!assignFile(p, ch, &err) && firstError.isEmpty()) firstError = err;
    }
    if (!firstError.isEmpty()) { lastMessage_.clear(); report(firstError); }
}

void StereoRouter::setChannel(const QString& key, Channel ch) {
    const int i = indexOf(key);
    if (i < 0 || assignments_[i].channel == ch) return;
    if (!canPlace(key, ch)) { reportEarFull(ch); return; }
    assignments_[i].channel = ch;
    persist();
    emit assignmentsChanged();
    reconcile();
}

void StereoRouter::unassign(const QString& key) {
    const int i = indexOf(key);
    if (i < 0) return;
    removeAssignments({key});     // drops the capture and gives the app its normal sound back
}

void StereoRouter::removeAssignments(const QStringList& keys) {
    bool any = false;
    for (const QString& key : keys) {
        const int i = indexOf(key);
        if (i < 0) continue;
        assignments_.removeAt(i);
        running_.remove(key);
        absent_.remove(key);
        graceUntil_.remove(key);
        for (int k = pending_.size() - 1; k >= 0; --k) if (pending_[k].key == key) pending_.removeAt(k);
        any = true;
    }
    if (!any) return;
    forcePrune_ = true;
    persist();
    emit assignmentsChanged();
    updateWindowList();
    reconcile();
}

void StereoRouter::swapSides() {
    if (assignments_.isEmpty()) return;
    for (auto& a : assignments_) a.channel = opposite(a.channel);
    persist();
    emit assignmentsChanged();
    reconcile();
}

void StereoRouter::setEarVolume(Channel c, int percent) {
    percent = qBound(0, percent, 100);
    volume_[static_cast<int>(c)] = percent;
    settings_->setEarVolume(c, percent);
    for (const auto& s : engine_->sources())
        engine_->setSourceGain(s.pid, gainFor(s.channel));
}

void StereoRouter::shutdown() {
    timer_.stop();
    engine_->stop();
}

void StereoRouter::onSourceFailed(quint32 pid, const QString& reason) {
    if (reason != "Process exited") {
        markFailed(pid);
        report(reason);
    } else {
        // The app was closed: check again shortly so its tile disappears within about a second.
        for (int ms : {400, 1300}) QTimer::singleShot(ms, this, &StereoRouter::reconcile);
    }
}

void StereoRouter::reconcile() {
    if (assignments_.isEmpty()) {
        running_.clear();
        failedPids_.clear();
        missing_.clear();
        forcePrune_ = false;
        if (engine_->state() != AudioEngine::State::Stopped) engine_->stop();
        return;
    }
    adoptLaunchedApp();

    auto st = engine_->state();
    if (st == AudioEngine::State::Stopped || st == AudioEngine::State::Error) {
        QString err;
        if (!engine_->start(QString(), 20, &err)) { report(err); return; }
        lastMessage_.clear();
        st = engine_->state();
    }
    if (st != AudioEngine::State::Running) return;      // Recovering: the engine retries by itself

    const auto parents = ProcessManager::parentMap();
    // Never capture a process that (indirectly) started StereoFlow: it would capture our own output.
    std::set<unsigned long> ancestorsOfUs;
    for (unsigned long cur = GetCurrentProcessId(); ancestorsOfUs.size() < 32;) {
        auto it = parents.find(cur);
        if (it == parents.end() || it->second == 0 || ancestorsOfUs.count(it->second)) break;
        cur = it->second;
        ancestorsOfUs.insert(cur);
    }
    QSet<quint32> wanted, seen;
    QSet<QString> running;
    QStringList gone;                                      // assigned apps that were closed
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const auto& a : assignments_) {
        const auto pids = ProcessManager::pidsForExe(a.exePath.toStdWString());
        if (pids.empty()) {
            // Closed (or never started): off its ear after two empty checks in a row, unless it is still starting.
            if (nowMs >= graceUntil_.value(a.key, 0) && ++absent_[a.key] >= 2) gone.append(a.key);
            continue;
        }
        absent_.remove(a.key);
        graceUntil_.remove(a.key);                          // seen alive: from now on closing it removes it
        running.insert(a.key);
        std::map<unsigned long, bool> cand;
        for (auto p : pids) { cand[p] = true; seen.insert(static_cast<quint32>(p)); }
        for (auto pid : pids) {
            // Capturing a process also captures its children, so only the top-most process is routed.
            if (ProcessManager::hasAncestorIn(pid, parents, cand)) continue;
            if (ancestorsOfUs.count(pid)) continue;     // started StereoFlow: capturing it would capture our own output (silently skipped)
            const quint32 p = static_cast<quint32>(pid);
            wanted.insert(p);
            if (!engine_->hasSource(p)) {
                if (inBackoff(p)) continue;
                QString err;
                if (!engine_->addSource(p, a.channel, gainFor(a.channel), false, &err)) {
                    markFailed(p);
                    report(a.name + ": " + err);
                }
            } else {
                engine_->setSourceChannel(p, a.channel);
                engine_->setSourceGain(p, gainFor(a.channel));
            }
        }
    }
    const bool runningChanged = (running != running_);
    running_ = running;
    for (auto it = failedPids_.begin(); it != failedPids_.end();) it = seen.contains(it.key()) ? ++it : failedPids_.erase(it);
    // A routed process that is missing from ONE snapshot (transient access error, parent/child hand-over) must not
    // cost a capture restart plus an un-hush/re-hush cycle: that is an audible cut. Dead processes are removed by the
    // capture thread itself; here we only drop a pid after it was missing twice in a row, or at once on unassign.
    for (quint32 pid : engine_->sourcePids()) {
        if (wanted.contains(pid)) { missing_.remove(pid); continue; }
        if (forcePrune_ || ++missing_[pid] >= 2) { missing_.remove(pid); engine_->removeSource(pid); }
    }
    forcePrune_ = false;
    for (auto it = missing_.begin(); it != missing_.end();) it = engine_->hasSource(it.key()) ? ++it : missing_.erase(it);
    if (!gone.isEmpty()) {
        // Remove them from the ears (and from the list in Applications); the capture is already gone with the process.
        removeAssignments(gone);                           // re-enters reconcile() once, with nothing left to remove
        return;
    }
    if (runningChanged) emit assignmentsChanged();
}

} // namespace sf

namespace sf {

// A shortcut can start a launcher (Update.exe, a game launcher, a stub) that opens the real app and exits. If the
// exe the shortcut points to never shows up but a new window does, route that app instead.
void StereoRouter::adoptLaunchedApp() {
    if (pending_.isEmpty()) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int k = pending_.size() - 1; k >= 0; --k) {
        const PendingLaunch p = pending_[k];
        const int ai = indexOf(p.key);
        if (ai < 0 || now > p.deadline) { pending_.removeAt(k); continue; }
        if (!ProcessManager::pidsForExe(assignments_[ai].exePath.toStdWString()).empty()) { pending_.removeAt(k); continue; }
        if (now - p.since < 2500) continue;
        for (const auto& w : windows_) {
            if (w.pid == 0 || p.windowsBefore.contains(w.key) || indexOf(w.key) >= 0) continue;
            Assignment& a = assignments_[ai];
            graceUntil_.remove(a.key);
            absent_.remove(a.key);
            a.key = w.key; a.name = w.appName; a.exePath = w.exePath;
            graceUntil_[a.key] = now + 3000;
            pending_.removeAt(k);
            persist();
            emit assignmentsChanged();
            updateWindowList(true);
            break;
        }
    }
}

} // namespace sf