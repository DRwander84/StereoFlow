# StereoFlow Architecture

```
UI (Qt)        MainWindow (frameless, custom title bar, 3 tabs) -> MixerPage, ApplicationsPage, SettingsPage; Widgets.cpp = painted controls
Routing        StereoRouter   assignments (exe path -> ear), ear volumes, auto start/stop
Audio engine   AudioEngine -- AudioMixer + AudioRenderer (render thread), ProcessLoopbackCapture per app tree
System         SessionVolumeGuard + RecoveryJournal (mute/restore original sound), WindowsVersion
Core           WindowEnumerator (open windows), ProcessManager, SessionEnumeration, DeviceMonitor
```

## Flow
1. **Applications** tab lists visible top-level windows (`WindowEnumerator`). Store apps are resolved to the real app process.
2. Pressing Left/Right creates an assignment keyed by the lower-case exe path. Several apps may share an ear.
3. Every 2 s (and on each change) `StereoRouter::reconcile()` finds all running processes of each assigned exe, keeps only the top-most process of each tree, and adds/removes capture sources in the engine. Apps without a window (tray apps) still work.
4. Each source is captured with process-loopback (the process and its children), converted to mono, resampled and mixed to **one** ear only. Ear volume = gain of every source on that ear.
5. The original sound is silenced by **hushing** (not muting) the audio sessions of the whole process tree to 0.0016 % volume (-96 dB, below the 16-bit floor of the output, so nothing is left in the other ear; the guard reads the volume back and steps to -72 / -48 dB only if Windows does not keep it), and the captured copy is boosted by exactly 62500x. Reason: Windows applies the session mute/volume *before* the process-loopback tap, so a mute also silences the capture. Hushing happens at once when a source is added (no waiting for audible sound), every 100 ms for new sessions, and immediately after a mixer trip. Sessions already at the hush volume (left by an earlier run, Windows remembers per-app volume) are adopted, and sessions whose volume an app put back are hushed again. After a hush the boost stays off for 250 ms so loud audio already in the pipe is not amplified. The mixer drops the boost instantly if a block is louder than a hushed capture can be (ceiling grows with the number of hushed sessions); the boost always comes back, there is no permanent "unboosted" state. The journal on disk restores the original volumes after a crash.
6. Last assignment removed or app closed: boost off first, then the original volume is restored.

Rules unchanged from the audio core: no allocation/locks in the render path, Left source writes exactly 0 to the right bus (unit-tested), limiter per ear.

## Windows constraints
* Process loopback capture works from Windows 10 2004 (build 19041).
* The captured copy follows the app's session volume/mute (verified on hardware: muting silenced the capture), hence hush + boost instead of mute.
* Float capture (32-bit) is used so that the -96 dB / +96 dB round trip is lossless. The original stays at -96 dB in both ears: below the 16-bit floor of the output.
* StereoFlow refuses to route a process that started it (it would capture its own output).

## Isolation of the two ears
* Every source has its own ring, resampler, gain and boost state; the mixer sums left sources into the left bus and right sources into the right bus, each with its own limiter.
* The render thread is wait-free: it reads one raw pointer to the source list and never frees memory (replaced lists are kept ~2 s and freed on the UI thread). Flush-to-zero/denormals-are-zero is set on that thread.
* Hush/boost state is per source. One enumeration of audio sessions per 300 ms serves all sources. A mixer "trip" (block louder than a hushed capture can be) only re-settles that one source.
* Events (source added/removed, new session hushed, trips, calibration) are written to `%LOCALAPPDATA%\StereoFlow\stereoflow.log` to diagnose glitches.

## Apps on the ears
* An app that is closed (no process of its exe for two checks in a row, about 1-2 s) is removed from its ear and from the list, not dimmed. Apps being launched, and apps restored at start-up, get a grace period (30 s / 6 s).
* Drop an .exe or .lnk anywhere on the window (left half = left ear, right half = right ear) or on an ear. Shortcuts are resolved with IShellLink; the app is started if it is not running and appears in Applications at once with its ear. If a shortcut starts a launcher and another exe shows up, the routing follows the real app.
* Explorer cannot drop onto an elevated (administrator) window: StereoFlow warns if it was started that way.

## Hush robustness (v2.1)
* **Per-app hush depth.** The depth (-96 / -72 / -48 dB) is decided per routed app by the guard, never globally. A read-back that differs is retried 3 times before the depth is changed (an app or the user's volume slider writing at the same moment used to push *every* app to -48 dB for the rest of the run, audible in both ears), and a shallower depth is probed again after 120 s. A depth change only re-settles the app it belongs to.
* **Scoped scans.** A trip re-checks only the tripped app instead of enumerating every session of every app each 50 ms; the process snapshot is cached ~150 ms (refreshed at once for an unknown pid).
* **Leftover release.** Sessions sitting at a hush depth that no routed app owns are restored to full volume: at start-up, every 2 s while running, in `restoreAll()`, and by a headless `StereoFlow.exe --restore-watch` that is started when the app quits (30 min max, ends when StereoFlow starts again). Reason: Windows remembers the volume per app, so an app closed while hushed started almost silent, in both ears.
