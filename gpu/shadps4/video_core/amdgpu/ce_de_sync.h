// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: upstream #5406, kept independent of CE task ownership and scheduling.
#pragma once

#include <coroutine>
#include <cstdint>

namespace AmdGpu::CeDeSync {

constexpr bool WaitForDe(std::uint32_t ce, std::uint32_t de, std::uint32_t limit) {
    return ce >= de && ce - de >= limit;
}

inline bool ResumeCe(std::uint32_t ce, std::uint32_t de, std::coroutine_handle<> task) {
    return ce <= de && task && !task.done();
}

} // namespace AmdGpu::CeDeSync
