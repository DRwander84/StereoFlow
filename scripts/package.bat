@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM ============================================================================
REM  StereoFlow: one-shot packaging (build + tests + Setup + Portable exe + Portable zip)
REM
REM  Usage (run from the project root):
REM    .\scripts\package.bat -QtDir "C:\Qt\6.7.3\msvc2022_64" -InnoDir "C:\Program Files (x86)\Inno Setup 6"
REM
REM  Options:
REM    -QtDir   <path>   Qt MSVC 64-bit kit (folder that contains bin\qmake.exe)       [or env QT_DIR]
REM    -InnoDir <path>   Inno Setup 6 folder (contains ISCC.exe)                       [auto-detected if omitted]
REM    -Version <x.y.z>  Version number used in file names (default: read from CMakeLists.txt)
REM    -Config  <cfg>    Release (default) or RelWithDebInfo
REM    -SkipTests        Do not run ctest
REM    -SkipBuild        Reuse the existing build\vs2022 output
REM
REM  Output (in .\dist):
REM    StereoFlow-<ver>-win64-setup.exe      installer
REM    StereoFlow-<ver>-win64-portable.exe   portable self-extracting (pick a folder)
REM    StereoFlow-<ver>-win64-portable.zip   portable zip (unzip and run)
REM ============================================================================

set "QTDIR_ARG="
set "INNODIR_ARG="
set "VER="
set "CONFIG=Release"
set "SKIPTESTS=0"
set "SKIPBUILD=0"

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="-QtDir"      (set "QTDIR_ARG=%~2" & shift & shift & goto parse)
if /i "%~1"=="-InnoDir"    (set "INNODIR_ARG=%~2" & shift & shift & goto parse)
if /i "%~1"=="-Version"    (set "VER=%~2" & shift & shift & goto parse)
if /i "%~1"=="-Config"     (set "CONFIG=%~2" & shift & shift & goto parse)
if /i "%~1"=="-SkipTests"  (set "SKIPTESTS=1" & shift & goto parse)
if /i "%~1"=="-SkipBuild"  (set "SKIPBUILD=1" & shift & goto parse)
echo [ERROR] Unknown option: %~1
goto usage
:parsed

REM ---- go to project root (parent of this script's folder) -------------------
set "ROOT="
if exist "%~dp0..\CMakeLists.txt" set "ROOT=%~dp0.."
if "%ROOT%"=="" if exist "%~dp0CMakeLists.txt" set "ROOT=%~dp0"
if "%ROOT%"=="" if exist "%CD%\CMakeLists.txt" set "ROOT=%CD%"
if "%ROOT%"=="" (
  echo [ERROR] CMakeLists.txt not found next to or above this script, or in the current folder.
  echo         Put package.bat in the "scripts" folder of the project, e.g. D:\StereoFlow\scripts\package.bat
  echo         Script location: %~dp0
  exit /b 1
)
pushd "%ROOT%" || (echo [ERROR] Cannot enter project root & exit /b 1)
echo Project root: %CD%

REM ---- Qt ----------------------------------------------------------------------
if not "%QTDIR_ARG%"=="" set "QT_DIR=%QTDIR_ARG%"
if "%QT_DIR%"=="" (echo [ERROR] -QtDir is required. & goto usage_fail)
if "%QT_DIR:~-1%"=="\" set "QT_DIR=%QT_DIR:~0,-1%"
if not exist "%QT_DIR%\bin\qmake.exe" (echo [ERROR] Qt not found: "%QT_DIR%\bin\qmake.exe" is missing. & goto fail)

REM ---- Inno Setup --------------------------------------------------------------
set "ISCC="
if not "%INNODIR_ARG%"=="" (
  if "%INNODIR_ARG:~-1%"=="\" set "INNODIR_ARG=%INNODIR_ARG:~0,-1%"
  if exist "%INNODIR_ARG%\ISCC.exe" set "ISCC=%INNODIR_ARG%\ISCC.exe"
  if "!ISCC!"=="" (echo [ERROR] ISCC.exe not found in "%INNODIR_ARG%". & goto fail)
)
if "%ISCC%"=="" for %%P in ("%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe" "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe") do if exist %%P set "ISCC=%%~P"
if "%ISCC%"=="" for /f "delims=" %%I in ('where ISCC.exe 2^>nul') do set "ISCC=%%I"
if "%ISCC%"=="" (echo [ERROR] Inno Setup 6 not found. Pass -InnoDir or install it from https://jrsoftware.org/isdl.php & goto fail)

REM ---- version (from CMakeLists.txt "project(StereoFlow VERSION x.y.z") ------------
if "%VER%"=="" for /f "tokens=4 delims=() " %%V in ('findstr /R /C:"^project(StereoFlow VERSION" CMakeLists.txt') do set "VER=%%V"
if "%VER%"=="" set "VER=1.0.0"
echo %VER%| findstr /R /C:"^[0-9][0-9.]*$" >nul || set "VER=1.0.0"

set "BUILD=build\vs2022"
set "BIN=%BUILD%\bin\%CONFIG%"
echo ==============================================================
echo  StereoFlow %VER%  (%CONFIG%)
echo  Qt   : %QT_DIR%
echo  Inno : %ISCC%
echo ==============================================================

REM ---- build -------------------------------------------------------------------
if "%SKIPBUILD%"=="1" goto afterbuild

where cmake >nul 2>nul || (echo [ERROR] cmake not found in PATH. & goto fail)
set "GEN="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo [ERROR] Visual Studio 2022+ with "Desktop development with C++" is required. & goto fail)
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion`) do set "VSYEAR=%%V"
if "%VSYEAR%"=="2022" set "GEN=Visual Studio 17 2022"
if "%VSYEAR%"=="2026" set "GEN=Visual Studio 18 2026"
if "%VSYEAR%"=="18"   set "GEN=Visual Studio 18 2026"
if "%GEN%"=="" (echo [ERROR] No Visual Studio with C++ tools found. & goto fail)
echo [1/5] Configure with %GEN%

REM A cache from another generator would stop CMake: start clean in that case.
if exist "%BUILD%\CMakeCache.txt" findstr /C:"CMAKE_GENERATOR:INTERNAL=%GEN%" "%BUILD%\CMakeCache.txt" >nul || rmdir /s /q "%BUILD%"

cmake -S . -B "%BUILD%" -G "%GEN%" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" || goto fail
echo [2/5] Build
cmake --build "%BUILD%" --config %CONFIG% --parallel || goto fail
if "%SKIPTESTS%"=="0" (
  echo [3/5] Tests
  ctest --test-dir "%BUILD%" -C %CONFIG% --output-on-failure || goto fail
) else (
  echo [3/5] Tests skipped
)
:afterbuild

if not exist "%BIN%\StereoFlow.exe" (echo [ERROR] %BIN%\StereoFlow.exe not found. & goto fail)

REM windeployqt needs VCINSTALLDIR to copy the MSVC runtime (vcruntime/msvcp) next to the exe.
if "%VCINSTALLDIR%"=="" (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "!VSWHERE!" for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCINSTALLDIR=%%I\VC\"
)

REM ---- deploy Qt + MSVC runtime next to the exe (nothing to install on the target PC) ----
echo [4/5] Deploying Qt runtime
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "%BIN%\StereoFlow.exe" || goto fail

REM ---- installers --------------------------------------------------------------
echo [5/5] Building Setup + Portable
if not exist dist mkdir dist
del /q "dist\StereoFlow-%VER%-win64-*" >nul 2>nul

"%ISCC%" /Qp "installer\StereoFlow.iss" /DMyVersion=%VER% "/DBinDir=..\%BIN%" || goto fail
"%ISCC%" /Qp "installer\StereoFlow.iss" /DMyVersion=%VER% "/DBinDir=..\%BIN%" /DPORTABLE=1 || goto fail

REM Portable zip (same files, no extractor needed).
set "ZDIR=dist\StereoFlow-%VER%-win64-portable"
if exist "%ZDIR%" rmdir /s /q "%ZDIR%"
mkdir "%ZDIR%" || goto fail
xcopy "%BIN%\*" "%ZDIR%\" /E /I /Y /Q /EXCLUDE:installer\zip_exclude.txt >nul || goto fail
copy /Y LICENSE "%ZDIR%\" >nul
copy /Y NOTICE  "%ZDIR%\" >nul
copy /Y README.md "%ZDIR%\" >nul
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%ZDIR%\*' -DestinationPath '%ZDIR%.zip' -Force" || goto fail
rmdir /s /q "%ZDIR%"

echo.
echo ==============================================================
echo  DONE. Files in %CD%\dist :
echo ==============================================================
dir /b dist
popd
endlocal & exit /b 0

:usage
echo.
:usage_fail
echo Usage: .\scripts\package.bat -QtDir "C:\Qt\6.7.3\msvc2022_64" -InnoDir "C:\Program Files (x86)\Inno Setup 6" [-Version 1.0.0] [-SkipTests] [-SkipBuild]
:fail
echo.
echo [FAILED] Packaging stopped.
popd
endlocal & exit /b 1
