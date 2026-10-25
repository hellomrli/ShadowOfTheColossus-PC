// SPDX-License-Identifier: GPL-2.0-or-later
#include "../bbgpu.h"
#include "video_core/gpu_diagnostics.h"
#include <cstdio>

extern "C" void bbgpu_dump_diagnostic_context(void) {
    const auto* scope = SotcGpuDiagnostics::current;
    if (!scope) {
        std::fprintf(stderr, "GPU context: no instrumented operation on the faulting thread\n");
        return;
    }
    for (unsigned depth = 0; scope && depth < 8; ++depth, scope = scope->previous) {
        std::fprintf(stderr, "GPU context #%u: %s", depth, scope->operation ? scope->operation : "unnamed operation");
        if (scope->stage >= 0) {
            std::fprintf(stderr, " shader=0x%016llx permutation=%llu hw_stage=%d",
                         static_cast<unsigned long long>(scope->shader),
                         static_cast<unsigned long long>(scope->permutation), scope->stage);
        }
        if (scope->callback) {
            std::fprintf(stderr, " recorder_callback=%p", reinterpret_cast<void*>(scope->callback));
        }
        if (scope->keys) {
            std::fprintf(stderr, " pipeline_stage_keys=");
            for (std::size_t i = 0; i < scope->count && i < 8; ++i) {
                std::fprintf(stderr, "%s0x%016llx", i ? "," : "",
                             static_cast<unsigned long long>(scope->keys[i]));
            }
        }
        std::fprintf(stderr, "\n");
    }
}
