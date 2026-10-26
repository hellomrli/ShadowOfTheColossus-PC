// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include "gpu/shadps4/video_core/amdgpu/ce_de_sync.h"

using namespace AmdGpu::CeDeSync;

struct Task {
    struct promise_type {
        Task get_return_object() {
            return {std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(int) noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::abort(); }
    };
    std::coroutine_handle<promise_type> handle;
    ~Task() { handle.destroy(); }
};

static Task ConstantWork(std::uint32_t& ce, std::uint32_t& de, int& writes) {
    ++ce;
    while (WaitForDe(ce, de, 1)) {
        co_yield 0;
    }
    ++writes;
    ++ce;
}

static Task NestedWork(std::uint32_t& ce, std::uint32_t& de, int& writes) {
    auto child = ConstantWork(ce, de, writes);
    while (!child.handle.done()) {
        child.handle.resume();
        if (!child.handle.done()) {
            co_yield 0;
        }
    }
}

int main() {
    static_assert(!WaitForDe(1, 0, 2)); // Formerly 0 - 1 wrapped to UINT32_MAX.
    static_assert(WaitForDe(2, 0, 2));
    static_assert(WaitForDe(3, 0, 2));
    static_assert(!WaitForDe(0, 1, 1));
    static_assert(!WaitForDe(1, 1, 1));
    static_assert(WaitForDe(1, 1, 0));
    constexpr auto max = std::numeric_limits<std::uint32_t>::max();
    static_assert(!WaitForDe(max, max - 1, 2));
    static_assert(WaitForDe(max, 0, max));
    static_assert(!WaitForDe(0, max, 1));

    assert(!ResumeCe(0, 0, {})); // No CCB: do not query done() on a null handle.
    assert(!ResumeCe(0, 1, {}));
    assert(!ResumeCe(1, 0, {}));
    for (bool nested : {false, true}) {
        std::uint32_t ce = 0, de = 0;
        int writes = 0;
        auto task = nested ? NestedWork(ce, de, writes) : ConstantWork(ce, de, writes);
        assert(ResumeCe(ce, de, task.handle));
        task.handle.resume();
        assert(ce == 1 && writes == 0 && !task.handle.done());
        assert(!ResumeCe(ce, de, task.handle));
        de = 1;
        assert(ResumeCe(ce, de, task.handle));
        task.handle.resume();
        assert(task.handle.done() && writes == 1 && ce == 2);
        de = 2;
        assert(!ResumeCe(ce, de, task.handle)); // Never resume a completed coroutine.
    }
    {
        std::uint32_t ce = 0, de = 0;
        int writes = 0;
        auto task = NestedWork(ce, de, writes);
        task.handle.resume(); // Destroy a suspended nested wait, without resuming it.
        assert(writes == 0);
    }
    std::puts("CE/DE guards: unsigned boundaries, absent/completed CE and nested resume OK");
}
