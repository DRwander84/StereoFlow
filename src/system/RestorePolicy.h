#pragma once
#include "audio/Suppression.h"
#include <cmath>

namespace sf {

// Decides whether StereoFlow should un-mute a session when restoring (legacy mute logic, kept tested).
constexpr bool shouldUnmuteOnRestore(bool originalMuted, bool appliedMuted, bool currentMuted) noexcept {
    return !originalMuted && appliedMuted && currentMuted;
}

// Restore the original session volume only if we hushed it and the user has not touched it since.
inline bool shouldRestoreVolume(bool applied, float currentVolume) noexcept {
    return applied && isHushLevel(currentVolume);
}

// A session sitting exactly at one of OUR hush depths that no routed app owns is a leftover (the app was closed
// while hushed and Windows remembered the volume, StereoFlow crashed, ...). It must be given its sound back, otherwise
// the app is almost inaudible in BOTH ears. Never touches system sounds or anything inside a routed process tree.
inline bool shouldReleaseOrphan(bool isSystemSounds, bool inProtectedTree, float currentVolume) noexcept {
    return !isSystemSounds && !inProtectedTree && isHushLevel(currentVolume);
}

} // namespace sf
