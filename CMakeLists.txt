cmake_minimum_required(VERSION 3.24)
project(StereoFlow VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(STEREOFLOW_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
  add_compile_options($<$<CONFIG:Release>:/O2>)
  add_compile_options($<$<CONFIG:Release>:/Gy>)
  add_link_options($<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Portable real-time audio core (no Windows, no Qt): mixer, resampler, ring buffer,
# channel mapping, recovery journal. Builds and is unit-tested on any platform.
# ---------------------------------------------------------------------------
add_library(stereoflow_core STATIC
  src/audio/AudioMixer.cpp
  src/audio/AudioSource.cpp
  src/audio/AudioResampler.cpp
  src/routing/ChannelMapping.cpp
  src/system/RecoveryJournal.cpp)
target_include_directories(stereoflow_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(stereoflow_core PUBLIC Threads::Threads)

if(STEREOFLOW_BUILD_TESTS)
  enable_testing()
  foreach(t AudioMixerTests ChannelMappingTests AudioFormatTests SessionRecoveryTests RoutingTests)
    add_executable(${t} tests/${t}.cpp)
    target_include_directories(${t} PRIVATE tests)
    target_link_libraries(${t} PRIVATE stereoflow_core)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Windows application
# ---------------------------------------------------------------------------
if(WIN32)
  enable_language(RC)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

  add_executable(StereoFlow WIN32
    src/main.cpp
    src/app/Application.cpp src/app/Application.h
    src/audio/AudioEngine.cpp src/audio/AudioEngine.h
    src/audio/AudioRenderer.cpp src/audio/AudioRenderer.h
    src/audio/ProcessLoopbackCapture.cpp src/audio/ProcessLoopbackCapture.h
    src/core/SessionEnumeration.cpp src/core/SessionEnumeration.h
    src/core/ProcessManager.cpp src/core/ProcessManager.h
    src/core/ShortcutResolver.cpp src/core/ShortcutResolver.h
    src/core/WindowEnumerator.cpp src/core/WindowEnumerator.h
    src/core/DeviceMonitor.cpp src/core/DeviceMonitor.h
    src/routing/StereoRouter.cpp src/routing/StereoRouter.h
    src/system/SessionVolumeGuard.cpp src/system/SessionVolumeGuard.h
    src/system/WindowsVersion.cpp src/system/WindowsVersion.h
    src/utils/AppSettings.cpp src/utils/AppSettings.h
    src/ui/Theme.h
    src/ui/Widgets.cpp src/ui/Widgets.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/MixerPage.cpp src/ui/MixerPage.h
    src/ui/ApplicationsPage.cpp src/ui/ApplicationsPage.h
    src/ui/SettingsPage.cpp src/ui/SettingsPage.h
    src/utils/Log.h src/audio/Suppression.h
    assets/icon/StereoFlow.rc)

  target_include_directories(StereoFlow PRIVATE src)
  target_compile_definitions(StereoFlow PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                                                _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(StereoFlow PRIVATE stereoflow_core Qt6::Widgets
    ole32 oleaut32 uuid mmdevapi avrt version shell32 user32 gdi32 dwmapi advapi32)

  # Optional fonts (e.g. Inter-*.ttf): everything in assets/fonts is copied next to the exe and loaded at start.
  add_custom_command(TARGET StereoFlow POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:StereoFlow>/fonts")

  # Collect Qt runtime next to the exe (windeployqt) after build.
  get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
  find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
  if(WINDEPLOYQT)
    add_custom_command(TARGET StereoFlow POST_BUILD
      COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
              $<$<CONFIG:Release>:--release> $<TARGET_FILE:StereoFlow>
      COMMENT "Deploying Qt runtime")
  endif()
else()
  message(STATUS "StereoFlow GUI is Windows-only; only the portable core and tests are built.")
endif()
