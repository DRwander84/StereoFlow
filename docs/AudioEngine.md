# Audio Engine

Public API (`AudioEngine`): `start`, `stop`, `pause`, `resume`, `addSource`, `removeSource`, `setSourceChannel`, `setSourceGain`, `setSourceMuted`, `setOutputDevice`, `reinitialiseDevice`, plus failure handling for device loss, capture failure, process exit and render failure.

## Mixer
* Sources are mono float at the device rate. Gain is ramped linearly across each block (no zipper noise).
* Left sources add into the left bus only, right sources into the right bus only. Multiple sources per channel are summed.
* Per-channel limiter: threshold 0.98, instant attack, ~100 ms release, hard clamp at ±1. Transparent (bit-exact) below threshold.
* Meters: true block peak of each source (pre-gain) with decay; output peaks per channel.
* Loops are simple and branch-light so MSVC `/O2` auto-vectorises them (SSE2/AVX). No hand-written intrinsics.

## Resampler
Streaming 4-point Catmull-Rom cubic interpolation, passthrough when rates match. Good for speech/game audio; it is not a mastering-grade polyphase filter (some aliasing above ~18 kHz when down-sampling 48k->44.1k).

## Buffers and latency
Capture ring 65536 frames per source (~1.3 s headroom, overflow counted, never blocks). Adaptive jitter buffer per source: starts at 25 ms, grows 15 ms (max 120 ms) after each real underrun (stream still arriving), shrinks 5 ms per 8 s without one, and a backlog above target+90 ms (clock drift) is trimmed. Capture threads run with MMCSS "Audio". Typical total = WASAPI buffer (default ≥ device period, 20 ms setting) + 10 ms pre-fill + capture packet (~10 ms).

## Failure handling
| Event | Reaction |
|---|---|
| Process exits | capture thread sees process handle / stream error -> source removed, original audio restored, UI notified |
| Capture error | same, with HRESULT text |
| Render failure / device invalidated | all muted originals restored immediately, renderer stopped, engine -> *Recovering*, retries every 1.5 s for ~60 s (if Automatic recovery is on), then re-mutes |
| Default device changed (and "Follow Windows default" is selected) | `reinitialiseDevice()` |
| Mono output device | warning shown, L+R downmixed |
| Unsupported device format (not float32) | start fails with clear message |
