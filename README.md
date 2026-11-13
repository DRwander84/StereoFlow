<p align="center">
  <img src="assets/icon/StereoFlow.png" alt="StereoFlow" width="128" height="128">
</p>

<h1 align="center">StereoFlow</h1>

<p align="center"><b>Hear multiple sounds at once with a single headset.</b></p>


Send each app's sound to **one ear** of your headphones. Discord on the right, your game on the left, nothing leaking to the other side.

* Windows 10 (2004 / build 19041) or newer, x64. No driver, no admin rights, no network.
* Up to **5 apps per ear**. Drag an `.exe` or shortcut onto an ear, or pick apps in the *Applications* tab. One volume slider per ear.
* C++20, Qt 6 Widgets, WASAPI process loopback.

## Download
From the [Releases](../../releases) page:
* **Setup**: `StereoFlow-<version>-win64-setup.exe`, run it and follow the wizard (no admin rights needed).
* **Portable**: `StereoFlow-<version>-win64-portable.zip` (unzip anywhere) or `...-portable.exe` (pick a folder), then run `StereoFlow.exe`. Nothing is installed; delete the folder to remove it.

## Build
Install once: Visual Studio 2022 (Desktop development with C++), CMake 3.24+, Qt 6.5+ (MSVC 2022 64-bit) and [Inno Setup 6](https://jrsoftware.org/isdl.php).

Open **CMD** or **PowerShell**, go to the project folder and run (use your own paths):
```
.\scripts\package.bat -QtDir "C:\Qt installation path\version folder\msvc2022_64" -InnoDir "Inno Setup 6 installation path"
```
This builds the app, runs the tests and creates the Setup and Portable files. Optional: `-Version 1.0.0`, `-SkipTests`, `-SkipBuild`.

Output in `dist\`:
* `StereoFlow-1.0.0-win64-setup.exe` (installer)
* `StereoFlow-1.0.0-win64-portable.exe` (portable, unpacks into a folder you pick)
* `StereoFlow-1.0.0-win64-portable.zip` (portable zip)

Upload them to a GitHub Release, or push a tag (`git tag v1.0.0` then `git push origin v1.0.0`) and the included workflow builds and publishes them for you.

Details in `docs/`. Tests of the portable audio core: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.

## Recommended Channel

By visiting the [PC Gaming Hub](https://t.me/pcgaminghub) Telegram channel, you can follow the latest gaming and technology news.

## Donation

If you would like to support me, you can send a donation to the following address:

`TMKpNvJ73RR6g2NQ3b18eAHZuXCkbiTUv7`

## License
[Apache License 2.0](LICENSE). Use it, change it, redistribute it, but **keep the [NOTICE](NOTICE)**, credit the original author and repository, and say what you changed.

Created by **DRwander** ([@DRwander84](https://github.com/DRwander84)).