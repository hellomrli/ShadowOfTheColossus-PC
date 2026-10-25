// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: crash-only, per-thread GPU context; no allocation, logging or cross-thread locks.
#pragma once

#include <cstddef>
#include <cstdint>

namespace SotcGpuDiagnostics {

class Scope;
inline thread_local const Scope* current = nullptr;

class Scope {
public:
    explicit Scope(const char* operation_, std::uint64_t shader_ = 0,
                   std::uint64_t permutation_ = 0, int stage_ = -1,
                   std::uintptr_t callback_ = 0)
        : operation{operation_}, shader{shader_}, permutation{permutation_}, stage{stage_},
          callback{callback_}, previous{current} {
        current = this;
    }

    Scope(const char* operation_, const std::size_t* keys_, std::size_t count_)
        : Scope{operation_} {
        keys = keys_;
        count = count_;
    }

    ~Scope() {
        current = previous;
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    const char* operation;
    std::uint64_t shader;
    std::uint64_t permutation;
    int stage;
    std::uintptr_t callback;
    const Scope* previous;
    const std::size_t* keys{};
    std::size_t count{};
};

} // namespace SotcGpuDiagnostics
