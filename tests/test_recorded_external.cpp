// SPDX-License-Identifier: GPL-2.0-or-later
// Run the production command recorder against a fake driver; no Vulkan device or NGX needed.
#include <cassert>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "common/thread.h"
#include "video_core/gpu_diagnostics.h"
#include "video_core/host_cpu_usage.h"
#include "video_core/renderer_vulkan/vk_recorder.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// The standalone target does not link the loader or alter the test runner's thread priorities.
namespace Common {
void SetCurrentThreadName(const char*) {}
void SetCurrentThreadPriority(ThreadPriority) {}
} // namespace Common

void assert_fail_impl() { std::abort(); }
[[noreturn]] void unreachable_impl() { std::abort(); }

namespace {
using Vulkan::CommandRecorder;

int handle_storage;
const auto real = reinterpret_cast<VkCommandBuffer>(&handle_storage);
std::thread::id owner;
std::vector<u32> events;

void VKAPI_CALL DriverDraw(VkCommandBuffer command, u32 event, u32, u32, u32) {
    assert(command == real);
    assert(!CommandRecorder::IsFake(command));
    assert(std::this_thread::get_id() == owner);
    events.push_back(event);
}

void TestInline() {
    owner = std::this_thread::get_id();
    events.clear();
    for (const bool expected : {true, false}) {
        const bool result = CommandRecorder::RecordSynchronous(
            vk::CommandBuffer{real}, [value = std::make_unique<bool>(expected)](auto command) {
                DriverDraw(command, 7, 0, 0, 0);
                return *value;
            });
        assert(result == expected);
    }
    assert((events == std::vector<u32>{7, 7}));
}

void TestRecorded(CommandRecorder& recorder, bool create_ok, bool evaluate_ok) {
    events.clear();
    const auto producer = std::this_thread::get_id();
    const auto fake = recorder.NewFakeHandle();
    assert(CommandRecorder::IsFake(fake));
    recorder.Run([&recorder, fake, producer] {
        assert(recorder.IsRecordingThread());
        owner = std::this_thread::get_id();
        assert(owner != producer);
        recorder.Bind(fake, real);
        events.push_back(0);
    });
    // These commands pass through the real dispatcher thunk, just like the input conversions.
    // Enough commands cross a chunk boundary as well as the publish-batch boundary.
    constexpr u32 conversions = 6000;
    for (u32 i = 1; i <= conversions; ++i) {
        fake.draw(i, 0, 0, 0);
    }

    bool created = false;
    bool evaluated = false;
    bool capture_destroyed = false;
    struct Capture {
        bool& destroyed;
        ~Capture() { destroyed = true; }
    };
    const bool result = CommandRecorder::RecordSynchronous(
        fake, [&, capture = std::make_unique<Capture>(capture_destroyed)](auto command) {
            assert(recorder.IsRecordingThread());
            assert(command == vk::CommandBuffer{real});
            assert(events.size() == conversions + 1);
            for (u32 i = 0; i <= conversions; ++i) {
                assert(events[i] == i);
            }
            assert(SotcGpuDiagnostics::current);
            const SotcGpuDiagnostics::Scope diagnostic{"mock NGX create/evaluate"};
            // The SDK calls the driver directly, without the Vulkan-Hpp dispatcher thunks.
            DriverDraw(command, conversions + 1, 0, 0, 0);
            created = true;
            if (!create_ok) {
                return false;
            }
            DriverDraw(command, conversions + 2, 0, 0, 0);
            evaluated = true;
            // A nested real-handle call runs inline, rather than deadlocking on Sync().
            return CommandRecorder::RecordSynchronous(vk::CommandBuffer{command}, [&](auto nested) {
                assert(recorder.IsRecordingThread());
                assert(nested == command);
                return evaluate_ok;
            });
        });
    assert(result == (create_ok && evaluate_ok));
    assert(created && evaluated == create_ok);
    assert(capture_destroyed);
    assert(SotcGpuDiagnostics::current == nullptr);

    // Output transition or fallback must follow the SDK call, on the same recording thread.
    fake.draw(result ? 9000 : 9001, 0, 0, 0);
    recorder.Run([&recorder] { recorder.Retire(1); });
    recorder.Sync();
    assert(events.size() == conversions + (create_ok ? 4 : 3));
    assert(events.back() == (result ? 9000 : 9001));
}

void TestWakeups(CommandRecorder& recorder) {
    using namespace std::chrono_literals;
    const std::array<unsigned char, 128 * 1024> padding{};
    for (int round = 0; round < 30; ++round) {
        recorder.Sync();
        // Alternate publication during the spin/park transition and while fully idle.
        if (round % 2) {
            std::this_thread::sleep_for(10ms);
        }
        std::promise<void> done;
        auto completion = done.get_future();
        recorder.Run([&] {
            assert(recorder.IsRecordingThread());
            done.set_value();
        });
        switch (round % 3) {
        case 0:
            recorder.Kick();
            break;
        case 1:
            for (int i = 0; i < 31; ++i) {
                recorder.Run([] {});
            }
            break;
        case 2:
            // No Kick or full command batch: only the chunk's End publishes the callback.
            (void)recorder.Copy(padding.data(), padding.size());
            (void)recorder.Copy(padding.data(), padding.size());
            break;
        }
        assert(completion.wait_for(2s) == std::future_status::ready);
        completion.get();
        recorder.Sync();
    }
}

void MeasureIdleCpu(CommandRecorder& recorder) {
    recorder.Sync();
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const auto before = SotcCpu::Sample();
    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    const auto usage = SotcCpu::Between(before, SotcCpu::Sample());
    const double percent = usage.thread_percent[static_cast<std::size_t>(SotcCpu::Role::Recorder)];
    assert(percent >= 0);
    // Informational measurement, not a timing-sensitive pass threshold on shared CI runners.
    std::printf("recorder idle CPU: %.2f%% of one logical CPU (250 ms empty-queue sample)\n", percent);
}
} // namespace

int main() {
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdDraw = &DriverDraw;
    TestInline(); // SOTC_RECORD_THREAD=0: no recorder exists.
    {
        CommandRecorder recorder;
        TestInline(); // Other schedulers can still use real handles with a recorder alive.
        for (int i = 0; i < 3; ++i) {
            TestRecorded(recorder, true, true);
            TestRecorded(recorder, false, true);
            TestRecorded(recorder, true, false);
        }
        TestWakeups(recorder);
        MeasureIdleCpu(recorder);
    }
    for (int i = 0; i < 4; ++i) {
        // Shutdown must wake a parked recorder and release its last stream chunks.
        CommandRecorder recorder;
        recorder.Sync();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    TestInline();
    std::puts("recorded external calls: real handles, ordering, ownership, results and lifetime OK");
    std::puts("recorder wakeups: explicit kick, batch, chunk boundary and idle shutdown OK");
}
