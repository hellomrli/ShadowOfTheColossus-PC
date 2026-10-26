// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>
#include "gpu/shadps4/video_core/texture_cache/image_content.h"

using namespace VideoCore;

static void TestContents(u32 raw, unsigned samples) {
    const auto source_flags = static_cast<ImageFlagBits>(raw);
    // Software upload/copy callbacks model two old mips and a newly allocated third mip.
    const std::array<int, 2> old_gpu{7, 8};
    const std::array<int, 3> fresh_guest{41, 42, 43};
    std::array<int, 3> destination{};
    ImageFlagBits dest_flags = ImageFlagBits::Dirty | ImageFlagBits::Registered;
    std::vector<std::function<void()>> commands;
    std::vector<int> order;
    RefreshExpandedContents(source_flags, [&] {
        order.push_back(1);
        if (samples > 1) { // RefreshImage does not upload multisampled guest images.
            return;
        }
        commands.emplace_back([&] { destination = fresh_guest; });
        dest_flags &= ~ImageFlagBits::Dirty;
    }, [&] {
        order.push_back(2);
        commands.emplace_back([&] { std::copy(old_gpu.begin(), old_gpu.end(), destination.begin()); });
        dest_flags |= ImageFlagBits::GpuModified;
        dest_flags &= ~ImageFlagBits::Dirty;
    });
    const bool valid_gpu = (raw & 8u) != 0 && (raw & 7u) == 0;
    assert(HasValidGpuContents(source_flags) == valid_gpu);
    assert(order == (valid_gpu ? std::vector<int>{1, 2} : std::vector<int>{1}));
    assert((destination == std::array<int, 3>{})); // No early execution of deferred commands.
    for (auto& command : commands) {
        command();
    }
    if (valid_gpu) {
        assert(destination[0] == 7 && destination[1] == 8);
        assert(destination[2] == (samples == 1 ? 43 : 0));
        assert(HasValidGpuContents(dest_flags));
    } else {
        assert(destination == (samples == 1 ? fresh_guest : std::array<int, 3>{}));
        assert(False(dest_flags & ImageFlagBits::GpuModified));
        assert(True(dest_flags & ImageFlagBits::Dirty) == (samples > 1));
    }
    assert(True(dest_flags & ImageFlagBits::Registered));
}

int main() {
    // Include every dirty/modified combination and unrelated Registered/Picked bits.
    for (u32 flags = 0; flags < 256; ++flags) {
        TestContents(flags, 1);
        TestContents(flags, 4);
    }
    bool copied = false;
    try {
        RefreshExpandedContents(ImageFlagBits::GpuModified,
                                [] { throw std::runtime_error("upload failed"); },
                                [&] { copied = true; });
        assert(false);
    } catch (const std::runtime_error&) {
        assert(!copied);
    }
    std::puts("Image expansion policy: dirty-state matrix, ordered deferred copies, mips and MSAA OK");
}
