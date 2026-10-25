// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/gpu_diagnostics.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    assert(SotcGpuDiagnostics::current == nullptr);
    const std::size_t keys[] = {0x1234, 0x5678};
    {
        const SotcGpuDiagnostics::Scope shader{"compile shader", 0x9abc, 4, 3};
        assert(SotcGpuDiagnostics::current == &shader);
        std::thread worker{[] {
            assert(SotcGpuDiagnostics::current == nullptr);
            const SotcGpuDiagnostics::Scope own{"worker", 42, 0, 1};
            assert(SotcGpuDiagnostics::current == &own);
        }};
        worker.join();
        assert(SotcGpuDiagnostics::current == &shader);
        {
            const SotcGpuDiagnostics::Scope pipeline{"pipeline", keys, 2};
            assert(SotcGpuDiagnostics::current == &pipeline);
            assert(pipeline.previous == &shader);
            assert(pipeline.keys[1] == 0x5678);
        }
        assert(SotcGpuDiagnostics::current == &shader);
        try {
            const SotcGpuDiagnostics::Scope failure{"failed operation"};
            throw std::runtime_error("test");
        } catch (const std::runtime_error&) {
            assert(SotcGpuDiagnostics::current == &shader);
        }
    }
    assert(SotcGpuDiagnostics::current == nullptr);
    std::cout << "GPU diagnostic context passed\n";
}
