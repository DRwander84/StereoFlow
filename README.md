<p align="center">
  <img src="assets/icon/StereoFlow.png" alt="StereoFlow" width="128" height="128">
</p>

<h1 align="center">StereoFlow</h1>

<p align="center"><b>Hear multiple sounds at once with a single headset.</b></p>


Send each app's sound to **one ear** of your headphones. Discord on the right, your game on the left, nothing leaking to the other side.

* Windows 10 (2004 / build 19041) or newer, x64. No driver, no admin rights, no network.
* Up to **5 apps per ear**. Drag an `.exe` or shortcut onto an ear, or pick apps in the *Applications* tab. One volume slider per ear.

## Build
Install once: Visual Studio 2022/2026 (Desktop development with C++), CMake 3.24+, Qt 6.5+ (MSVC 2022 64-bit) and [Inno Setup 6](https://jrsoftware.org/isdl.php).

Open **CMD** or **PowerShell**, go to the project folder and run (use your own paths):
```
.\scripts\package.bat -QtDir "C:\Qt installation path\version folder\msvc2022_64" -InnoDir "Inno Setup 6 installation path"
```
Output in `dist\`:
* `StereoFlow-1.0.0-win64-setup.exe` (installer)
* `StereoFlow-1.0.0-win64-portable.exe` (portable, unpacks into a folder you pick)
* `StereoFlow-1.0.0-win64-portable.zip` (portable zip)

## Recommended Channel

By visiting the [PC Gaming Hub](https://t.me/pcgaminghub) Telegram channel, you can follow the latest gaming and technology news.

## Donation

If you would like to support me, you can send a donation to the following address:

`TMKpNvJ73RR6g2NQ3b18eAHZuXCkbiTUv7`

## License
[Apache License 2.0](LICENSE). Use it, change it, redistribute it, but **keep the [NOTICE](NOTICE)**, credit the original author and repository, and say what you changed.

Created by **DRwander** ([@DRwander84](https://github.com/DRwander84)).