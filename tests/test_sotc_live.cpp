// SPDX-License-Identifier: GPL-2.0-or-later
// Upscaler selection and fallback need no Vulkan device, DLLs or game dump.
#include <cassert>
#include <cstdio>
#include <initializer_list>

#include "gpu/shadps4/video_core/sotc_live.h"

int main(int argc, char** argv) {
    using namespace SotcLive;
    if (argc == 2) {
        assert(static_cast<int>(GetUpscaler()) == std::atoi(argv[1]));
    }
    for (const bool legacy_fsr1 : {false, true}) {
        assert(ParseUpscaler("dlss", legacy_fsr1) == Upscaler::Dlss);
        assert(ParseUpscaler("fsr1", legacy_fsr1) == Upscaler::Fsr1);
        assert(ParseUpscaler("bilinear", legacy_fsr1) == Upscaler::Bilinear);
    }
    assert(ParseUpscaler("") == Upscaler::Dlss);
    assert(ParseUpscaler("", false) == Upscaler::Bilinear);
    assert(ParseUpscaler("unknown") == Upscaler::Dlss);

    // DLSS always keeps FSR 1 ready for menus, movies, HDR and capability/evaluation failures.
    assert(UsesFsr1(Upscaler::Dlss));
    assert(UsesFsr1(Upscaler::Fsr1));
    assert(!UsesFsr1(Upscaler::Bilinear));
    for (const auto mode : {Upscaler::Dlss, Upscaler::Fsr1, Upscaler::Bilinear}) {
        upscaler.store(static_cast<int>(mode));
        assert(GetUpscaler() == mode);
    }

    // RCAS attenuation and DLSS sharpness run in opposite directions.
    assert(DlssSharpness(true, 0.0f) == 1.0f);
    assert(DlssSharpness(true, 0.25f) == 0.75f);
    assert(DlssSharpness(true, 0.6f) < DlssSharpness(true, 0.25f));
    assert(DlssSharpness(false, 0.0f) == 0.0f);
    assert(DlssSharpness(true, -1.0f) == 1.0f);
    assert(DlssSharpness(true, 2.0f) == 0.0f);
    std::puts("SotC live settings: selection, fallback and sharpening PASS");
}
