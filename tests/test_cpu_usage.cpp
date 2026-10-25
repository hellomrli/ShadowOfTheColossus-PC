// SPDX-License-Identifier: GPL-2.0-or-later
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <latch>
#include <thread>
#include "gpu/shadps4/video_core/host_cpu_usage.h"

int main() {
    using namespace SotcCpu;
    constexpr std::uint64_t second = 1000000000;
    constexpr auto recorder = static_cast<std::size_t>(Role::Recorder);
    Snapshot a{}, b{};
    a.wall_ns = second;
    b.wall_ns = 61 * second;
    a.logical_processors = b.logical_processors = 12;
    a.process = {second, 0, true};
    b.process = {241 * second, 0, true};
    a.threads[recorder] = {second, 1, true};
    b.threads[recorder] = {31 * second, 1, true};
    auto usage = Between(a, b);
    assert(std::abs(usage.process_percent - 100.0 / 3.0) < 0.001);
    assert(usage.thread_percent[recorder] == 50);
    assert(usage.thread_percent[0] == -1); // No fabricated zero for unregistered threads.
    b.threads[recorder].generation = 2;
    assert(Between(a, b).thread_percent[recorder] == -1);
    b.threads[recorder] = {0, 1, true};
    assert(Between(a, b).thread_percent[recorder] == -1);
    b.logical_processors = 6;
    assert(Between(a, b).process_percent == -1);
    b.wall_ns = a.wall_ns;
    assert(Between(a, b).process_percent == -1);

    // Exercise real OS clocks and registration while the worker is alive and during teardown.
    std::latch ready{1}, start{1}, done{1}, release{1};
    std::atomic<std::uint64_t> sink{};
    std::jthread worker([&] {
        ThreadScope scope{Role::Recorder};
        ready.count_down();
        start.wait();
        std::uint64_t value = 1;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
        while (std::chrono::steady_clock::now() < end) {
            for (int i = 0; i < 1000; ++i) {
                value = value * 6364136223846793005ULL + 1;
            }
            sink.store(value, std::memory_order_relaxed);
        }
        done.count_down();
        release.wait();
    });
    ready.wait();
    const auto before = Sample();
    start.count_down();
    done.wait();
    const auto after = Sample();
    assert(before.process.valid && after.process.valid);
    assert(before.threads[recorder].valid && after.threads[recorder].valid);
    assert(after.threads[recorder].ns >= before.threads[recorder].ns);
    assert(Between(before, after).thread_percent[recorder] >= 0);
    release.count_down();
    // Sampling and thread removal can overlap without accessing a closed handle.
    for (int i = 0; i < 100; ++i) {
        (void)Sample();
    }
    worker.join();
    assert(!Sample().threads[recorder].valid);
    {
        ThreadScope first{Role::Recorder};
        const auto old = Sample();
        {
            ThreadScope replacement{Role::Recorder};
            const auto current = Sample();
            assert(current.threads[recorder].generation != old.threads[recorder].generation);
            assert(Between(old, current).thread_percent[recorder] == -1);
        }
        assert(!Sample().threads[recorder].valid);
    }
    std::puts("CPU usage: normalized process/thread time, clock validity and lifetime OK");
}
