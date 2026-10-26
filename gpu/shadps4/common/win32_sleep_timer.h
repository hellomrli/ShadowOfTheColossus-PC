// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: Win32 adapter shared by AccurateSleep and the native no-GPU regression test.
#pragma once

#include <windows.h>
#include "common/sleep_timer.h"

namespace Common::Detail {

struct Win32SleepBackend {
    using Handle = HANDLE;

    auto Now() const {
        return std::chrono::steady_clock::now();
    }

    Handle Create(bool high_resolution) const {
        if (high_resolution) {
            return CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
        }
        return CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    bool Arm(Handle handle, std::int64_t relative_ticks) const {
        LARGE_INTEGER interval{};
        interval.QuadPart = relative_ticks;
        return SetWaitableTimer(handle, &interval, 0, nullptr, nullptr, FALSE) != 0;
    }

    TimerWaitResult Wait(Handle handle, bool interruptible) const {
        const auto result = WaitForSingleObjectEx(handle, INFINITE, interruptible);
        if (result == WAIT_OBJECT_0) {
            return TimerWaitResult::Completed;
        }
        return result == WAIT_IO_COMPLETION ? TimerWaitResult::Interrupted : TimerWaitResult::Failed;
    }

    TimerWaitResult SleepMilliseconds(std::uint32_t ms, bool interruptible) const {
        const auto result = SleepEx(ms, interruptible);
        if (result == 0) {
            return TimerWaitResult::Completed;
        }
        return result == WAIT_IO_COMPLETION ? TimerWaitResult::Interrupted : TimerWaitResult::Failed;
    }

    void Close(Handle handle) const {
        CloseHandle(handle);
    }
};

inline bool Win32AccurateSleep(std::chrono::nanoseconds duration, std::chrono::nanoseconds* remaining,
                               bool interruptible) {
    thread_local SleepTimer<Win32SleepBackend> timer;
    return timer.SleepFor(duration, remaining, interruptible);
}

} // namespace Common::Detail
