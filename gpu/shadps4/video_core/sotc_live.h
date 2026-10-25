// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: settings the in-game menu changes while playing (bbport_overlay.cpp, "Quick settings").
// -1: the value from the environment at start (the launcher's choice); else the menu's.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string_view>

namespace SotcLive {

/// High quality textures: how many reports a texture bank stays sampled (SOTC_LOD_LINGER).
inline std::atomic<int> lod_linger{-1};

/// Texture level "Maximum" turns the page pool switch off (SOTC_LOD_POOL_LOW): its threshold in
/// percent, 0 off.
inline std::atomic<int> lod_pool_low{-1};

/// RCAS sharpening: attenuation in thousandths (0 high, 250 medium, 600 low), -2 off
/// (BB_RCAS=0). -1: the launcher's at start.
inline std::atomic<int> rcas{-1};

enum class Upscaler { Dlss, Fsr1, Bilinear };

/// One atomic choice keeps the presenter, input capture and menu on the same mode.
/// -1 reads SOTC_UPSCALER at startup; SOTC_DLSS=0 separately disables NGX initialization.
inline std::atomic<int> upscaler{-1};

constexpr Upscaler ParseUpscaler(std::string_view value, bool legacy_fsr1 = true) {
    if (value == "dlss") {
        return Upscaler::Dlss;
    }
    if (value == "fsr1") {
        return Upscaler::Fsr1;
    }
    if (value == "bilinear") {
        return Upscaler::Bilinear;
    }
    return legacy_fsr1 ? Upscaler::Dlss : Upscaler::Bilinear;
}

inline Upscaler GetUpscaler() {
    const int live = upscaler.load(std::memory_order_relaxed);
    if (live >= 0 && live <= static_cast<int>(Upscaler::Bilinear)) {
        return static_cast<Upscaler>(live);
    }
    static const Upscaler startup = [] {
        const char* mode = std::getenv("SOTC_UPSCALER");
        const char* fsr1 = std::getenv("BB_FSR1");
        const bool legacy_fsr1 = !fsr1 || fsr1[0] == '1' || fsr1[0] == 'y' || fsr1[0] == 't';
        return ParseUpscaler(mode ? mode : "", legacy_fsr1);
    }();
    return startup;
}

constexpr bool UsesFsr1(Upscaler mode) {
    // DLSS needs the FSR 1 fallback for unsupported GPUs and frames without temporal inputs.
    return mode != Upscaler::Bilinear;
}

constexpr float DlssSharpness(bool enabled, float rcas_attenuation) {
    return enabled ? 1.0f - std::clamp(rcas_attenuation, 0.0f, 1.0f) : 0.0f;
}

/// Far sun shadow cascades drawn on alternate frames (SOTC_SHADOW_REUSE): 0 off, 1 on.
inline std::atomic<int> shadow_reuse{-1};

} // namespace SotcLive
