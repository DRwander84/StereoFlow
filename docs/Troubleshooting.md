# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| "Unsupported Windows version" | Needs Windows 10 build 19041 (2004) or newer, or Windows 11. |
| Compile error: `audioclientactivationparams.h` not found | Windows SDK older than 10.0.19041. Install a newer SDK. |
| App not in the Applications tab | Only visible windows are listed. Open it and press **Refresh**. Windows of elevated (admin) apps cannot be read. |
| Status line says "Audio capture initialisation failed" | The process is protected or exited. Press Refresh and try again. |
| Routed sound is silent for a moment when a new tab/stream starts | Normal: the new session is hushed and the boost waits 250 ms (longer after repeated trips) for loud audio to drain. |
| Routed sound stays very quiet | Look at `stereoflow.log` for repeated "boost tripped" lines: an app keeps resetting its own volume or the capture does not follow the session volume. Set `suppressOriginal=false` under `[audio]` in `%APPDATA%\StereoFlow\StereoFlow.ini` to hear the routed sound at full level (the original then plays in both ears). |
| An app stays very quiet after a crash | Start StereoFlow once: the journal restores the original volume. Or raise the app volume in the Windows volume mixer. |
| Sound in both ears | The original is not muted yet (app opened a new audio session; it is muted within 2 s) or `suppressOriginal=false`. |
| Mono output device | Left/right separation is impossible on a mono device. |

