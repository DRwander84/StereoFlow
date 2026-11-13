#include "audio/AudioEngine.h"
#include "audio/Suppression.h"
#include "core/ProcessManager.h"
#include "utils/Log.h"
#include <QMetaObject>
#include <algorithm>

namespace sf {

static QString hrText(HRESULT hr) { return QString::fromStdWString(audioHrMessage(hr)); }

AudioEngine::AudioEngine(SessionVolumeGuard* guard, QObject* parent) : QObject(parent), guard_(guard) {
    qRegisterMetaType<sf::AudioEngine::State>("sf::AudioEngine::State");
    recoveryTimer_.setInterval(1500);
    connect(&recoveryTimer_, &QTimer::timeout, this, &AudioEngine::attemptRecovery);
    suppressTimer_.setInterval(50);
    connect(&suppressTimer_, &QTimer::timeout, this, &AudioEngine::serviceSuppression);
    clock_.start();
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

void AudioEngine::publishSources() {
    AudioMixer::SourceList list;
    for (auto it = sources_.begin(); it != sources_.end(); ++it) list.push_back(it->source);
    mixer_.setSources(std::move(list));
}

bool AudioEngine::startRenderer(QString* error) {
    renderer_ = std::make_unique<AudioRenderer>(mixer_, [this](HRESULT hr) {
        QMetaObject::invokeMethod(this, [this, hr] { handleRenderFailure(hr); }, Qt::QueuedConnection);
    });
    HRESULT hr = renderer_->start(deviceId_.toStdWString(), static_cast<unsigned>(bufferMs_));
    if (FAILED(hr)) {
        if (error) *error = "Audio output could not be started: " + hrText(hr);
        renderer_.reset();
        return false;
    }
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        it->source->setOutputRate(renderer_->sampleRate());
        it->source->requestFlush();
    }
    if (renderer_->isMono())
        emit problem("The output device is mono: physical left/right separation is not possible on this device.");
    emit deviceInfoChanged();
    return true;
}

void AudioEngine::stopRenderer() {
    if (renderer_) { renderer_->stop(); renderer_.reset(); }
}

bool AudioEngine::start(const QString& deviceId, int bufferMs, QString* error) {
    if (state_ == State::Running || state_ == State::Paused) return true;
    deviceId_ = deviceId;
    bufferMs_ = std::clamp(bufferMs, 10, 200);
    recoveryTimer_.stop();
    if (!startRenderer(error)) { setState(State::Error); return false; }
    publishSources();
    setState(State::Running);
    suppressTimer_.start();
    return true;
}

void AudioEngine::stop() {
    recoveryTimer_.stop();
    suppressTimer_.stop();
    removeAllSources();                       // stops captures, restores original audio
    stopRenderer();
    setState(State::Stopped);
    emit deviceInfoChanged();
}

bool AudioEngine::addSource(quint32 pid, Channel ch, float gain, bool muted, QString* error) {
    if (state_ != State::Running && state_ != State::Paused && state_ != State::Recovering) {
        if (error) *error = "Audio engine is not running";
        return false;
    }
    if (pid == 0) { if (error) *error = "Invalid process ID"; return false; }
    if (sources_.contains(pid)) { if (error) *error = "Duplicate session: process is already routed"; return false; }
    if (!ProcessManager::isAlive(pid)) { if (error) *error = "Process not found or already exited"; return false; }
    if (!renderer_ || !renderer_->running()) { if (error) *error = "Audio output is not ready"; return false; }

    Entry e;
    e.token = nextToken_++;
    e.source = std::make_shared<AudioSource>(nextSourceId_++, renderer_->sampleRate(), 1u << 16);
    e.source->setChannel(ch);
    e.source->setGain(gain);
    e.source->setMuted(muted);
    e.source->setCeiling(kHushedCeiling);
    const quint64 token = e.token;
    e.capture = std::make_shared<ProcessLoopbackCapture>(pid, e.source,
        [this, token](unsigned long p, CaptureFailure kind, HRESULT hr) {
            QMetaObject::invokeMethod(this, [this, p, token, kind, hr] {
                handleCaptureFailure(static_cast<quint32>(p), token, kind, hr);
            }, Qt::QueuedConnection);
        });

    // Transaction step 1: capture must be running BEFORE we touch the original audio.
    HRESULT hr = e.capture->start();
    if (FAILED(hr)) {
        if (error) *error = "Audio capture failed at step '" + QString::fromStdWString(e.capture->failureStage()) +
                            "': " + hrText(hr);
        return false;
    }
    if (!e.capture->isFloat()) {
        // 16-bit capture cannot carry a -96 dB hush (the copy would be quantised away). Keep the original audible.
        e.noHush = true;
        emit problem("This app can only be captured in 16-bit: left/right isolation is not possible for it.");
    }
    sources_.insert(pid, std::move(e));
    publishSources();
    scanNow_ = true;                       // hush its sessions on the very next tick, not up to 100 ms later
    logLine(QString("source added pid %1 -> %2").arg(pid).arg(ch == Channel::Left ? "left" : "right"));

    // The original is hushed by serviceSuppression() once capture and output are confirmed running.
    return true;
}

void AudioEngine::removeSource(quint32 pid) {
    auto it = sources_.find(pid);
    if (it == sources_.end()) return;
    logLine(QString("source removed pid %1").arg(pid));
    it->source->setBoost(1.0f);                       // never boost once the original is loud again
    it->source->setMuted(true);
    if (guard_) guard_->restoreProcess(pid);          // restore first: never leave an app silent
    it->capture->stop();
    sources_.erase(it);
    publishSources();
}

void AudioEngine::removeAllSources() {
    for (auto it = sources_.begin(); it != sources_.end(); ++it) { it->source->setBoost(1.0f); it->source->setMuted(true); }
    if (guard_) guard_->restoreAll();
    for (auto it = sources_.begin(); it != sources_.end(); ++it) it->capture->stop();
    sources_.clear();
    publishSources();
}

void AudioEngine::setSourceChannel(quint32 pid, Channel ch) { if (auto it = sources_.find(pid); it != sources_.end()) it->source->setChannel(ch); }
void AudioEngine::setSourceGain(quint32 pid, float g) { if (auto it = sources_.find(pid); it != sources_.end()) it->source->setGain(g); }
void AudioEngine::setSourceMuted(quint32 pid, bool m) { if (auto it = sources_.find(pid); it != sources_.end()) it->source->setMuted(m); }

QList<AudioEngine::SourceInfo> AudioEngine::sources() const {
    QList<SourceInfo> out;
    for (auto it = sources_.constBegin(); it != sources_.constEnd(); ++it) {
        SourceInfo s;
        s.pid = it.key();
        s.channel = it->source->channel();
        s.gain = it->source->gain();
        s.muted = it->source->muted();
        s.peak = it->source->peak();
        s.capturing = it->capture && it->capture->running();
        s.suppressed = it->suppressed;
        s.underruns = it->source->underruns();
        s.overflows = it->source->overflows();
        s.format = it->capture ? QString::fromStdWString(it->capture->formatDescription()) : QString();
        out.append(s);
    }
    return out;
}

void AudioEngine::handleCaptureFailure(quint32 pid, quint64 token, CaptureFailure kind, long hr) {
    auto it = sources_.find(pid);
    if (it == sources_.end() || it->token != token) return;   // stale notification
    removeSource(pid);
    emit sourceFailed(pid, kind == CaptureFailure::ProcessExited
                               ? QString("Process exited")
                               : "Audio capture failed: " + hrText(static_cast<HRESULT>(hr)));
}

void AudioEngine::handleRenderFailure(long hr) {
    if (state_ == State::Stopped) return;
    emit problem("Audio output failed: " + hrText(static_cast<HRESULT>(hr)));
    // Never leave the apps silent while we have no output.
    releaseAll();
    stopRenderer();
    if (autoRecovery_) {
        recoveryAttempts_ = 0;
        setState(State::Recovering);
        recoveryTimer_.start();
    } else {
        setState(State::Error);
    }
}

void AudioEngine::attemptRecovery() {
    ++recoveryAttempts_;
    QString err;
    if (startRenderer(&err)) {
        recoveryTimer_.stop();
        setState(State::Running);        // serviceSuppression() hushes the originals again
        emit problem("Audio output recovered.");
    } else if (recoveryAttempts_ >= 40) {   // ~60 s
        recoveryTimer_.stop();
        setState(State::Error);
        emit problem("Audio output could not be recovered: " + err);
    }
}

bool AudioEngine::reinitialiseDevice(QString* error) {
    if (state_ == State::Stopped) return true;
    releaseAll();
    stopRenderer();
    if (!startRenderer(error)) {
        if (autoRecovery_) { recoveryAttempts_ = 0; setState(State::Recovering); recoveryTimer_.start(); }
        else setState(State::Error);
        return false;
    }
    setState(State::Running);
    return true;
}
void AudioEngine::releaseAll() {
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        it->source->setBoost(1.0f);            // first: the restored (loud) original must not be boosted
        it->phase = Phase::Idle;
        it->suppressed = false;
    }
    if (guard_) guard_->restoreAll();
}

void AudioEngine::finishBoost(Entry& e) {
    // No flush here: dropping the buffer would cut the sound. The settle time already outlasts the pipeline depth
    // (jitter buffer <= 120 ms + capture/render buffers), and the mixer still trips on any loud block.
    e.source->setBoost(1.0f / e.hushVol);
    e.phase = Phase::Boosted;
}

void AudioEngine::serviceSuppression() {
    if (state_ != State::Running || !suppressOriginal_ || !guard_) return;
    mixer_.collectRetired();
    const qint64 now = clock_.elapsed();
    // Everything is re-checked every 100 ms. A trip of ONE app only re-checks that app (see below): a volume slider that
    // is being dragged for seconds must not make the other ear pay for it with an enumeration of every session every 50 ms.
    const bool scanAll = scanNow_ || (++tick_ % 2) == 0;
    scanNow_ = false;

    // 1) Per-source timers and safety trips: no COM calls, every tick. Sources never influence each other.
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        Entry& e = it.value();
        if (e.source->consumeTrip()) {
            // A block was louder than a hushed capture can be (a new, not yet hushed session, or the app put its volume
            // back). The mixer already played it at normal level and dropped the boost. Hush again right now, then
            // re-boost after the pipeline has drained. Repeated trips wait longer, but the boost always comes back:
            // there is no permanent "unboosted" state (that was what made apps almost inaudible).
            if (now - e.lastTrip > 10000) e.trips = 0;
            e.lastTrip = now;
            ++e.trips;
            e.settleMs = std::min<qint64>(250 * (qint64(1) << std::min(e.trips - 1, 3)), 2000);
            logLine(QString("pid %1: boost tripped (%2), re-arming in %3 ms").arg(it.key()).arg(e.trips).arg(e.settleMs));
            e.phase = Phase::Settling;
            e.t0 = now;
            e.scanSoon = true;
        }
        if (e.phase == Phase::Settling && now - e.t0 >= e.settleMs) finishBoost(e);
    }

    // 2) Hush the sessions of the routed apps that are due, with ONE enumeration (also catches sessions created later, and
    //    apps that put their volume back). No waiting for audible sound: hushing silence costs nothing and closes the leak.
    std::vector<unsigned long> roots;
    std::vector<quint32> ids;
    roots.reserve(sources_.size()); ids.reserve(sources_.size());
    for (auto it = sources_.begin(); it != sources_.end(); ++it) {
        const bool due = scanAll || it->scanSoon;
        it->scanSoon = false;
        if (it->noHush || !due) continue;
        roots.push_back(it.key()); ids.push_back(it.key());
    }
    if (roots.empty()) return;
    const auto results = guard_->suppressProcesses(roots);
    for (size_t k = 0; k < ids.size(); ++k) {
        auto it = sources_.find(ids[k]);
        if (it == sources_.end()) continue;
        Entry& e = it.value();
        const SuppressResult& r = results[k];
        if (r.total < 0) continue;                                    // enumeration failed: keep the current state
        if (r.level != e.hushLvl) {
            // Windows did not keep the deepest volume for THIS app and the guard stepped to another depth (or came back to
            // the deepest one): only this app's boost is stale. Every other app keeps playing untouched.
            logLine(QString("pid %1: hush depth changed to level %2 (%3)").arg(ids[k]).arg(r.level).arg(r.volume));
            e.hushLvl = r.level;
            e.hushVol = r.volume;
            e.source->setBoost(1.0f);
            if (e.phase != Phase::Idle) { e.phase = Phase::Settling; e.t0 = now; e.settleMs = 300; }
        }
        const float h = e.hushVol;
        if (r.newly > 0) logLine(QString("pid %1: hushed %2 session(s), %3 total").arg(ids[k]).arg(r.newly).arg(r.total));
        if (r.total > 0) e.suppressed = true;
        // Hushed sessions add up inside one capture (a browser with several tabs): allow for that before tripping.
        e.hushedSessions = r.total;
        e.source->setCeiling(std::clamp(1.25f * h * static_cast<float>(r.total + 1), 5.0f * h, 15.0f * h));
        if (r.total <= 0) continue;
        if (e.phase == Phase::Idle) {
            e.phase = Phase::Settling; e.t0 = now; e.settleMs = 250;  // loud audio captured before the hush is still in the pipe
        } else if (r.newly > 0 && e.phase == Phase::Boosted) {
            // A session was hushed just now: whatever it played before is loud. Boost off until that has drained.
            e.source->setBoost(1.0f);
            e.phase = Phase::Settling; e.t0 = now; e.settleMs = 250;
        } else if (r.newly > 0 && e.phase == Phase::Settling) {
            e.t0 = now;                                               // still changing: restart the drain timer
        }
    }
}

} // namespace sf
