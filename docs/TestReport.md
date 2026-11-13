# Test status

**Executed (Linux, g++, C++20): passed.** Portable audio core: ChannelMappingTests (6), AudioMixerTests (16), AudioFormatTests (23), SessionRecoveryTests (11), RoutingTests (4).

**Not compiled or run here** (no Windows/MSVC/Qt/audio device): window enumeration, router, process capture, WASAPI renderer, session guard, Qt UI.

## Hardware checklist
| # | Test | Expected |
|---|---|---|
| T1 | Build Release | no errors |
| T2 | Applications tab shows open windows with icons; Refresh adds a newly opened app | yes |
| T3 | Discord -> Right, game -> Left | each only in its own ear |
| T4 | Three apps right, two apps left, all playing | all heard in the right ear each, no crosstalk |
| T5 | Arrows in Stereo Mixer | all apps swap ears, sound moves |
| T6 | Drag an app to the other list | it switches ear |
| T7 | Original sound is muted but routed sound is audible | yes (else see Troubleshooting) |
| T8 | Browser (Chrome/Edge) tab playing audio | routed, original silent |
| T9 | Ear volume sliders | volume changes per ear live |
| T10 | Remove last app / close StereoFlow | all apps get normal sound back |
| T11 | Kill StereoFlow in Task Manager, start it again | apps un-muted by the journal |
| T12 | Unplug/replug headphones | recovers |
| T13 | Window cannot be resized, stays on top, drag by title bar | yes |
| T14 | Stop / change track in the app of one ear while the other plays | the other ear never drops or glitches |
| T15 | Drag an .exe / shortcut of a closed app onto an ear | app starts, is routed; of an open app: only routed |
