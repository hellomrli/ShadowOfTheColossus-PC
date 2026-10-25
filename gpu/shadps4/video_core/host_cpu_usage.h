// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: OS CPU time, sampled once per report rather than on every draw or command.
#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace SotcCpu {

enum class Role { Command, Draw, Recorder, Presenter, VideoOut, Count };
constexpr auto RoleCount = static_cast<std::size_t>(Role::Count);

struct Counter {
    std::uint64_t ns{};
    std::uint64_t generation{};
    bool valid{};
};

#ifdef _WIN32
using NativeClock = HANDLE;
inline Counter ReadClock(NativeClock clock, bool process = false) {
    FILETIME created, exited, kernel, user;
    const bool ok = process ? GetProcessTimes(clock, &created, &exited, &kernel, &user)
                            : GetThreadTimes(clock, &created, &exited, &kernel, &user);
    if (!ok) {
        return {};
    }
    const auto units = [](FILETIME t) {
        return (std::uint64_t{t.dwHighDateTime} << 32) | t.dwLowDateTime;
    };
    return {(units(kernel) + units(user)) * 100, 0, true};
}
#else
using NativeClock = clockid_t;
inline Counter ReadClock(NativeClock clock) {
    timespec time{};
    if (clock_gettime(clock, &time) != 0) {
        return {};
    }
    return {std::uint64_t(time.tv_sec) * 1000000000 + std::uint64_t(time.tv_nsec), 0, true};
}
#endif

struct Registry {
    struct Entry {
        NativeClock clock{};
        std::uint64_t generation{};
    };
    std::mutex mutex;
    std::array<Entry, RoleCount> entries{};
    std::uint64_t next_generation{};
};

inline Registry& Threads() {
    // Render threads can stop during static destruction; keep the registry alive until exit.
    static auto* registry = new Registry;
    return *registry;
}

class ThreadScope {
public:
    explicit ThreadScope(Role role_) : role{static_cast<std::size_t>(role_)} {
#ifdef _WIN32
        if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                             &clock, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            return;
        }
#else
        if (pthread_getcpuclockid(pthread_self(), &clock) != 0) {
            return;
        }
#endif
        auto& registry = Threads();
        std::scoped_lock lock{registry.mutex};
        generation = ++registry.next_generation;
        registry.entries[role] = {clock, generation};
    }

    ~ThreadScope() {
        if (!generation) {
            return;
        }
        auto& registry = Threads();
        {
            std::scoped_lock lock{registry.mutex};
            if (registry.entries[role].generation == generation) {
                registry.entries[role] = {};
            }
        }
#ifdef _WIN32
        CloseHandle(clock);
#endif
    }

    ThreadScope(const ThreadScope&) = delete;
    ThreadScope& operator=(const ThreadScope&) = delete;

private:
    std::size_t role;
    NativeClock clock{};
    std::uint64_t generation{};
};

inline unsigned LogicalProcessors() {
#ifdef _WIN32
    DWORD_PTR process{}, system{};
    if (GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process) {
        return std::popcount(static_cast<std::uint64_t>(process));
    }
#else
    cpu_set_t cpus;
    if (sched_getaffinity(0, sizeof(cpus), &cpus) == 0 && CPU_COUNT(&cpus)) {
        return CPU_COUNT(&cpus);
    }
#endif
    const auto count = std::thread::hardware_concurrency();
    return count ? count : 1;
}

struct Snapshot {
    std::uint64_t wall_ns{};
    unsigned logical_processors{};
    Counter process;
    std::array<Counter, RoleCount> threads{};
};

inline Snapshot Sample() {
    Snapshot result;
    result.wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch()).count();
    result.logical_processors = LogicalProcessors();
#ifdef _WIN32
    result.process = ReadClock(GetCurrentProcess(), true);
#else
    result.process = ReadClock(CLOCK_PROCESS_CPUTIME_ID);
#endif
    auto& registry = Threads();
    std::scoped_lock lock{registry.mutex};
    for (std::size_t i = 0; i < RoleCount; ++i) {
        const auto& entry = registry.entries[i];
        if (entry.generation) {
            result.threads[i] = ReadClock(entry.clock);
            result.threads[i].generation = entry.generation;
        }
    }
    return result;
}

struct Usage {
    double process_percent{-1};
    std::array<double, RoleCount> thread_percent;
};

inline Usage Between(const Snapshot& before, const Snapshot& after) {
    Usage result;
    result.thread_percent.fill(-1);
    if (after.wall_ns <= before.wall_ns) {
        return result;
    }
    const double scale = 100.0 / double(after.wall_ns - before.wall_ns);
    const auto valid = [](const Counter& a, const Counter& b) {
        return a.valid && b.valid && a.generation == b.generation && b.ns >= a.ns;
    };
    if (valid(before.process, after.process) && after.logical_processors &&
        before.logical_processors == after.logical_processors) {
        result.process_percent = (after.process.ns - before.process.ns) * scale /
                                 after.logical_processors;
    }
    for (std::size_t i = 0; i < RoleCount; ++i) {
        if (valid(before.threads[i], after.threads[i])) {
            result.thread_percent[i] = (after.threads[i].ns - before.threads[i].ns) * scale;
        }
    }
    return result;
}

} // namespace SotcCpu
