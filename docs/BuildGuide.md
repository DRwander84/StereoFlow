# Build Guide (Windows)

1. **Visual Studio 2022** (Community is fine). Workload *Desktop development with C++*; tick the latest *Windows 10/11 SDK* (must be 10.0.19041 or newer because `audioclientactivationparams.h` is required).
2. **CMake 3.24+** (add to PATH).
3. **Qt 6.5 or newer**: install with the Qt Online Installer, component *Qt 6.x -> MSVC 2022 64-bit*.
4. Open *x64 Native Tools Command Prompt for VS 2022* in the project folder:
   ```
   set QT_DIR=C:\Qt\6.7.3\msvc2022_64
   cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=%QT_DIR%
   cmake --build build\vs2022 --config Release
   ctest --test-dir build\vs2022 -C Release --output-on-failure
   ```
5. Output: `build\vs2022\bin\Release\StereoFlow.exe`. A post-build step runs `windeployqt`, so the folder is self-contained (Qt DLLs + `platforms\qwindows.dll`). Also ship the MSVC runtime (install *VC++ Redistributable x64*, or use `windeployqt --compiler-runtime`).
6. Setup + Portable files: `.\scripts\package.bat -QtDir "%QT_DIR%" -InnoDir "C:\Program Files (x86)\Inno Setup 6"` (see README).
7. Debug: same with `Debug`.

Other platforms: only the portable core and unit tests build (`cmake -S . -B build && ctest --test-dir build`).
