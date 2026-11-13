#pragma once
#include "audio/AudioMixer.h"
#include "audio/AudioRenderer.h"
#include "audio/ProcessLoopbackCapture.h"
#include "system/SessionVolumeGuard.h"
#include <QHash>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace sf {

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running, Paused, Recovering, Error };
    Q_ENUM(State)

    struct SourceInfo {
        quint32 pid = 0;
        Channel channel = Channel::Left;
        float gain = 1.0f;
        bool muted = false;
        float peak = 0.0f;
        bool capturing = false;
        bool suppressed = false;   // original audio muted by StereoFlow
        quint64 underruns = 0, overflows = 0;
        QString format;
    };

    explicit AudioEngine(SessionVolumeGuard* guard, QObject* parent = nullptr);
    ~AudioEngine() override;

    // ---- engine lifecycle ----
    bool start(const QString& deviceId, int bufferMs, QString* error);
    void stop();
    State state() const { return state_; }

    // ---- sources ----
    bool addSource(quint32 pid, Channel ch, float gain, bool muted, QString* error);
    void removeSource(quint32 pid);
    void removeAllSources();
    bool hasSource(quint32 pid) const { return sources_.contains(pid); }
    QList<quint32> sourcePids() const { return sources_.keys(); }
    void setSourceChannel(quint32 pid, Channel ch);
    void setSourceGain(quint32 pid, float gain);
    void setSourceMuted(quint32 pid, bool muted);
    QList<SourceInfo> sources() const;

    // ---- device ----
    void setSuppressOriginal(bool on) { suppressOriginal_ = on; }
    void setAutoRecovery(bool on) { autoRecovery_ = on; }
    bool reinitialiseDevice(QString* error = nullptr);
    QString outputDeviceId() const { return deviceId_; }

    // ---- info for UI ----
    uint32_t sampleRate() const { return renderer_ ? renderer_->sampleRate() : 0; }
    uint32_t outputChannels() const { return renderer_ ? renderer_->channels() : 0; }
    bool outputIsMono() const { return renderer_ && renderer_->isMono(); }
    double latencyMs() const { return renderer_ ? renderer_->latencyMs() : 0.0; }
    double bufferMs() const { return renderer_ ? renderer_->bufferMs() : 0.0; }
    float outputPeak(Channel c) const { return mixer_.outputPeak(c); }
    bool limiterEngaged() const { return mixer_.limiterEngaged(); }

signals:
    void stateChanged(sf::AudioEngine::State state);
    void sourceFailed(quint32 pid, const QString& reason);   // capture ended; source already removed
    void problem(const QString& message);                    // renderer / device level message
    void deviceInfoChanged();

private:
    // How the original app is silenced (see audio/Suppression.h).
    enum class Phase { Idle,          // original untouched
                       Settling,      // hushed, boost off; waiting for already-captured loud audio to leave the pipeline
                       Boosted };     // hushed original, captured copy boosted back to full level

    struct Entry {
        std::shared_ptr<AudioSource> source;
        std::shared_ptr<ProcessLoopbackCapture> capture;
        bool suppressed = false;
        quint64 token = 0;
        Phase phase = Phase::Idle;
        qint64 t0 = 0;                 // when the phase started (ms)
        qint64 settleMs = 250;         // how long Settling lasts (grows with repeated trips)
        qint64 lastTrip = 0; int trips = 0;            // mixer safety trips (see AudioMixer)
        int hushedSessions = 0;
        int hushLvl = 0;               // hush depth of THIS app (the guard decides it per routed app, apps never share it)
        float hushVol = kHushVolume;   // session volume of that depth; the boost is exactly 1 / hushVol
        bool scanSoon = false;         // this app only: re-check its sessions on the next tick (after a trip)
        bool noHush = false;           // capture is not float: a deep hush + boost would destroy it, so the original is left alone
    };
    void serviceSuppression();                 // every 50 ms: phase timers; every 100 ms (or at once after a trip) hush new sessions
    void finishBoost(Entry& e);
    void releaseAll();                         // boost off + restore every hushed app
    bool startRenderer(QString* error);
    void stopRenderer();
    void publishSources();
    void setState(State s);
    void handleCaptureFailure(quint32 pid, quint64 token, CaptureFailure kind, long hr);
    void handleRenderFailure(long hr);
    void attemptRecovery();

    SessionVolumeGuard* guard_;
    AudioMixer mixer_;
    std::unique_ptr<AudioRenderer> renderer_;
    QHash<quint32, Entry> sources_;
    State state_ = State::Stopped;
    QString deviceId_;
    int bufferMs_ = 20;
    bool suppressOriginal_ = true;
    bool autoRecovery_ = true;
    quint64 nextToken_ = 1;
    uint32_t nextSourceId_ = 1;
    QTimer recoveryTimer_;
    QTimer suppressTimer_;
    QElapsedTimer clock_;
    bool scanNow_ = false;                     // hush scan requested right away (new source, mixer trip)
    unsigned tick_ = 0;
    int recoveryAttempts_ = 0;
};

} // namespace sf

Q_DECLARE_METATYPE(sf::AudioEngine::State)
