// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: upstream #5435 timer reuse, with a testable backend and checked failure paths.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace Common::Detail {

enum class TimerWaitResult { Completed, Interrupted, Failed };

template <class Backend>
class SleepTimer {
public:
    explicit SleepTimer(Backend backend = {}) : backend(backend) {}
    SleepTimer(const SleepTimer&) = delete;
    SleepTimer& operator=(const SleepTimer&) = delete;

    ~SleepTimer() {
        Reset();
    }

    bool SleepFor(std::chrono::nanoseconds duration, std::chrono::nanoseconds* remaining,
                  bool interruptible) {
        if (duration.count() <= 0) {
            const auto result = backend.SleepMilliseconds(0, interruptible);
            if (remaining) {
                *remaining = std::chrono::nanoseconds::zero();
            }
            return result == TimerWaitResult::Completed;
        }
        if (active) {
            // An APC can sleep again; never rearm or close the timer of the outer wait.
            SleepTimer nested{backend};
            return nested.SleepFor(duration, remaining, interruptible);
        }
        struct ActiveWait {
            bool& active;
            explicit ActiveWait(bool& active) : active(active) { active = true; }
            ~ActiveWait() { active = false; }
        } active_wait{active};

        const auto begin = backend.Now();
        const auto time_left = [&] {
            const auto elapsed = backend.Now() - begin;
            return duration > elapsed ? duration - elapsed : std::chrono::nanoseconds::zero();
        };
        if (!handle) {
            handle = backend.Create(true);
            if (!handle) {
                handle = backend.Create(false);
            }
        }
        // Relative due times are negative 100 ns ticks; zero means an absolute time.
        const auto interval = -std::max<std::int64_t>(duration.count() / 100, 1);
        auto result = TimerWaitResult::Failed;
        if (handle && backend.Arm(handle, interval)) {
            result = backend.Wait(handle, interruptible);
        }
        if (result != TimerWaitResult::Completed) {
            Reset();
        }
        if (result == TimerWaitResult::Failed) {
            // An OS resource failure must not turn videoout pacing into a busy loop.
            result = FallbackSleep(time_left(), interruptible);
        }
        if (remaining) {
            *remaining = time_left();
        }
        return result == TimerWaitResult::Completed;
    }

private:
    TimerWaitResult FallbackSleep(std::chrono::nanoseconds duration, bool interruptible) {
        const auto ns = duration.count();
        auto ms = static_cast<std::uint64_t>(ns / 1'000'000 + (ns % 1'000'000 != 0));
        TimerWaitResult result;
        do {
            // Round up without overflow, and never pass Windows' INFINITE sentinel.
            const auto chunk = static_cast<std::uint32_t>(std::min(ms, std::uint64_t{0xfffffffe}));
            result = backend.SleepMilliseconds(chunk, interruptible);
            ms -= chunk;
        } while (ms && result == TimerWaitResult::Completed);
        return result;
    }

    void Reset() {
        if (handle) {
            backend.Close(handle);
            handle = {};
        }
    }

    [[no_unique_address]] Backend backend;
    typename Backend::Handle handle{};
    bool active{};
};

} // namespace Common::Detail
