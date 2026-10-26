// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <chrono>
#include <cstdio>
#include <functional>
#include <latch>
#include <map>
#include <thread>
#include <utility>
#include <vector>
#include "gpu/shadps4/common/sleep_timer.h"
#ifdef _WIN32
#include "gpu/shadps4/common/win32_sleep_timer.h"
#endif

using namespace std::chrono_literals;
using Common::Detail::SleepTimer;
using Common::Detail::TimerWaitResult;
using Ns = std::chrono::nanoseconds;

struct FakeState {
    std::chrono::steady_clock::time_point now{};
    bool high_res_ok = true, plain_ok = true, arm_ok = true;
    TimerWaitResult wait_result = TimerWaitResult::Completed;
    TimerWaitResult sleep_result = TimerWaitResult::Completed;
    Ns wait_elapsed{}, arm_elapsed{}, sleep_elapsed{};
    std::uintptr_t next_handle = 1;
    std::vector<bool> creates, alertable;
    std::vector<std::uintptr_t> waits, closed;
    std::vector<std::int64_t> intervals;
    std::vector<std::uint32_t> sleeps;
    std::map<std::uintptr_t, std::chrono::steady_clock::time_point> live;
    std::function<void()> on_wait;
};

struct FakeBackend {
    using Handle = std::uintptr_t;
    FakeState* state;

    auto Now() const { return state->now; }
    Handle Create(bool high_resolution) const {
        state->creates.push_back(high_resolution);
        if (!(high_resolution ? state->high_res_ok : state->plain_ok)) {
            return 0;
        }
        const auto handle = state->next_handle++;
        state->live.emplace(handle, state->now);
        return handle;
    }
    bool Arm(Handle handle, std::int64_t ticks) const {
        assert(state->live.contains(handle) && ticks < 0);
        state->intervals.push_back(ticks);
        state->now += state->arm_elapsed;
        state->live.at(handle) = state->now + Ns{-ticks * 100};
        return state->arm_ok;
    }
    TimerWaitResult Wait(Handle handle, bool interruptible) const {
        assert(state->live.contains(handle));
        state->waits.push_back(handle);
        state->alertable.push_back(interruptible);
        const auto result = std::exchange(state->wait_result, TimerWaitResult::Completed);
        const auto elapsed = std::exchange(state->wait_elapsed, Ns::zero());
        if (auto callback = std::exchange(state->on_wait, {})) {
            callback();
        }
        assert(state->live.contains(handle)); // A nested wait must not close its parent's handle.
        if (result == TimerWaitResult::Completed) {
            state->now = std::max(state->now, state->live.at(handle));
        }
        state->now += elapsed;
        return result;
    }
    TimerWaitResult SleepMilliseconds(std::uint32_t ms, bool interruptible) const {
        assert(ms != 0xffffffff);
        state->sleeps.push_back(ms);
        state->alertable.push_back(interruptible);
        const auto result = std::exchange(state->sleep_result, TimerWaitResult::Completed);
        state->now += result == TimerWaitResult::Completed ? std::chrono::milliseconds{ms}
                                                          : state->sleep_elapsed;
        return result;
    }
    void Close(Handle handle) const {
        assert(state->live.erase(handle) == 1);
        state->closed.push_back(handle);
    }
};

static void TestReuseAndFallback() {
    for (bool high_res : {false, true}) {
        FakeState state;
        state.high_res_ok = high_res;
        {
            SleepTimer timer{FakeBackend{&state}};
            for (int i = 0; i < 100; ++i) {
                Ns remaining{-1};
                assert(timer.SleepFor(2ms, &remaining, false));
                assert(remaining == 0ns);
            }
            assert(state.creates == (high_res ? std::vector<bool>{true}
                                             : std::vector<bool>{true, false}));
            assert(state.waits.size() == 100 && state.closed.empty());
            for (auto handle : state.waits) {
                assert(handle == state.waits.front());
            }
            assert(state.sleeps.empty());
        }
        assert(state.closed.size() == 1 && state.live.empty());
    }
    // Resource failure uses a finite, rounded-up blocking fallback, then retries next time.
    for (bool creation_failure : {false, true}) {
        FakeState state;
        state.high_res_ok = state.plain_ok = !creation_failure;
        state.arm_ok = creation_failure;
        SleepTimer timer{FakeBackend{&state}};
        Ns remaining{-1};
        assert(timer.SleepFor(1500us, &remaining, false));
        assert(state.sleeps == std::vector<std::uint32_t>{2});
        assert(remaining == 0ns && state.waits.empty() && state.live.empty());
        assert(state.closed.size() == (creation_failure ? 0 : 1));
        state.high_res_ok = state.plain_ok = state.arm_ok = true;
        assert(timer.SleepFor(1ms, nullptr, false));
        assert(state.live.size() == 1 && state.waits.size() == 1);
    }
    {
        FakeState state;
        state.wait_result = TimerWaitResult::Failed;
        state.wait_elapsed = 3ms;
        SleepTimer timer{FakeBackend{&state}};
        Ns remaining{-1};
        assert(timer.SleepFor(10ms, &remaining, true));
        assert(state.sleeps == std::vector<std::uint32_t>{7});
        assert(state.alertable == std::vector<bool>({true, true}));
        assert(state.closed.size() == 1 && remaining == 0ns);
    }
    {
        FakeState state;
        state.arm_ok = false;
        state.arm_elapsed = 20ms;
        SleepTimer timer{FakeBackend{&state}};
        Ns remaining{-1};
        assert(timer.SleepFor(10ms, &remaining, false));
        assert(state.sleeps == std::vector<std::uint32_t>{0});
        assert(remaining == 0ns && state.live.empty());
    }
    {
        FakeState state;
        state.high_res_ok = state.plain_ok = false;
        state.sleep_result = TimerWaitResult::Interrupted;
        state.sleep_elapsed = 2ms;
        SleepTimer timer{FakeBackend{&state}};
        Ns remaining{-1};
        assert(!timer.SleepFor(10ms, &remaining, true));
        assert(remaining == 8ms && state.sleeps.size() == 1);
        state.now = {};
        state.sleeps.clear();
        // A duration beyond DWORD's finite range must not become an infinite wait.
        assert(timer.SleepFor(std::chrono::milliseconds{0xfffffffeULL + 7}, &remaining, false));
        assert(state.sleeps == std::vector<std::uint32_t>({0xfffffffe, 7}));
        assert(remaining == 0ns);
    }
}

static void TestBoundsAndInterruption() {
    {
        FakeState state;
        SleepTimer timer{FakeBackend{&state}};
        for (const auto duration : {0ns, -1ns, Ns::min()}) {
            Ns remaining{-1};
            assert(timer.SleepFor(duration, &remaining, true));
            assert(remaining == 0ns && state.sleeps.back() == 0);
        }
        state.sleep_result = TimerWaitResult::Interrupted;
        Ns remaining{-1};
        assert(!timer.SleepFor(0ns, &remaining, true));
        assert(remaining == 0ns && state.creates.empty());
        for (const auto duration : {1ns, 99ns, 100ns, 199ns}) {
            assert(timer.SleepFor(duration, &remaining, false));
            assert(state.intervals.back() == -1);
            assert(remaining == (duration > 100ns ? duration - 100ns : 0ns));
        }
        state.wait_elapsed = 3ms;
        assert(timer.SleepFor(1ms, &remaining, false));
        assert(remaining == 0ns); // Oversleep cannot underflow remaining time.
    }
    {
        FakeState state;
        SleepTimer timer{FakeBackend{&state}};
        Ns remaining{-1};
        assert(timer.SleepFor(Ns::max(), &remaining, false));
        assert(state.intervals.back() == -(Ns::max().count() / 100));
        assert(remaining == Ns{Ns::max().count() % 100});
    }
    {
        FakeState state;
        SleepTimer timer{FakeBackend{&state}};
        state.wait_result = TimerWaitResult::Interrupted;
        state.wait_elapsed = 2ms;
        Ns remaining{-1};
        assert(!timer.SleepFor(10ms, &remaining, true));
        assert(remaining == 8ms && state.live.empty() && state.closed.size() == 1);
        assert(state.sleeps.empty()); // An APC must return, not finish the interrupted delay.
        assert(timer.SleepFor(20ms, &remaining, false));
        assert(state.waits[0] != state.waits[1] && remaining == 0ns);
        assert(state.alertable == std::vector<bool>({true, false}));
    }
}

static void TestReentrancyAndLifetime() {
    for (const auto result : {TimerWaitResult::Completed, TimerWaitResult::Interrupted}) {
        FakeState state;
        {
            SleepTimer timer{FakeBackend{&state}};
            state.wait_result = result;
            state.on_wait = [&] {
                const auto outer = state.waits.front();
                assert(timer.SleepFor(2ms, nullptr, false));
                assert(state.waits.back() != outer);
                assert(state.live.size() == 1 && state.live.contains(outer));
                assert(state.closed.size() == 1 && state.closed.front() != outer);
            };
            Ns remaining{-1};
            assert(timer.SleepFor(10ms, &remaining, true) == (result == TimerWaitResult::Completed));
            assert(remaining == (result == TimerWaitResult::Completed ? 0ns : 8ms));
        }
        assert(state.live.empty() && state.closed.size() == 2);
    }
    for (int i = 0; i < 16; ++i) {
        FakeState state;
        std::thread worker([&] {
            thread_local SleepTimer timer{FakeBackend{&state}};
            for (int j = 0; j < 10; ++j) {
                assert(timer.SleepFor(1ms, nullptr, false));
            }
            assert(state.live.size() == 1 && state.closed.empty());
        });
        worker.join();
        assert(state.creates.size() == 1 && state.closed.size() == 1 && state.live.empty());
    }
}

#ifdef _WIN32
using Common::Detail::Win32AccurateSleep;

static void CALLBACK NestedSleepApc(ULONG_PTR context) {
    auto* calls = reinterpret_cast<int*>(context);
    assert(Win32AccurateSleep(2ms, nullptr, false));
    ++*calls;
}

static void TestNativeWindows() {
    Ns remaining{-1};
    assert(Win32AccurateSleep(0ns, &remaining, false) && remaining == 0ns);
    assert(Win32AccurateSleep(-1ns, &remaining, false) && remaining == 0ns);
    assert(Win32AccurateSleep(1ns, &remaining, false));
    assert(remaining >= 0ns && remaining <= 1ns);
    // Queue before the alertable wait, so the APC does not depend on thread-start timing.
    std::latch ready{1}, queued{1};
    HANDLE thread_handle{};
    int apc_calls = 0;
    std::thread worker([&] {
        assert(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                               &thread_handle, THREAD_SET_CONTEXT, FALSE, 0));
        ready.count_down();
        queued.wait();
        Ns left{-1};
        assert(!Win32AccurateSleep(1s, &left, true));
        assert(apc_calls == 1 && left >= 0ns && left <= 1s);
        const auto start = std::chrono::steady_clock::now();
        assert(Win32AccurateSleep(5ms, &left, false));
        assert(std::chrono::steady_clock::now() - start >= 3ms);
        assert(left >= 0ns && left <= 5ms);
    });
    ready.wait();
    assert(QueueUserAPC(NestedSleepApc, thread_handle, reinterpret_cast<ULONG_PTR>(&apc_calls)));
    queued.count_down();
    worker.join();
    assert(CloseHandle(thread_handle));
    // After runtime warmup, short-lived threads must not each leak their cached timer.
    DWORD before{}, after{};
    assert(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 32; ++i) {
        std::thread sleeper([] {
            for (int j = 0; j < 4; ++j) {
                assert(Win32AccurateSleep(1ms, nullptr, false));
            }
        });
        sleeper.join();
    }
    assert(GetProcessHandleCount(GetCurrentProcess(), &after));
    assert(after <= before + 4);
    std::printf("Windows timers: APC reentry, subsequent wait and thread-exit handles OK (%lu -> %lu)\n",
                before, after);
}
#endif

int main() {
    TestReuseAndFallback();
    TestBoundsAndInterruption();
    TestReentrancyAndLifetime();
    std::puts("Sleep timer: reuse, high-res fallback, failures, boundaries, APC and TLS lifetime OK");
#ifdef _WIN32
    TestNativeWindows();
#endif
}
