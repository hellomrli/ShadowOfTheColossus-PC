// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/cache_blob.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
    std::vector<std::uint32_t> words{0xdeadbeef};
    const std::array<std::byte, 8> valid{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
                                         std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    assert(Storage::Blob::Read(words, valid.size(), [&](std::span<std::byte> out) {
        std::copy(valid.begin(), valid.end(), out.begin());
        return true;
    }));
    assert(words.size() == 2);

    for (const auto size : {std::uint64_t{0}, std::uint64_t{7}, Storage::Blob::MaxBytes + 4,
                           UINT64_MAX}) {
        bool reader_called = false;
        assert(!Storage::Blob::Read(words, size, [&](std::span<std::byte>) {
            reader_called = true;
            return true;
        }));
        assert(!reader_called);
        assert(words.empty());
    }
    words.push_back(1);
    assert(!Storage::Blob::Read(words, valid.size(), [](std::span<std::byte> target) {
        target.front() = std::byte{9}; // A short read or failed archive CRC must discard all output.
        return false;
    }));
    assert(words.empty());
    assert(!Storage::Blob::Read(words, Storage::Blob::MaxBytes + 4,
                                [](std::span<std::byte>) { return true; }));

    const auto root = std::filesystem::temp_directory_path() /
                      ("sotc-cache-blob-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(root));
    const auto path = root / "shader.spv";
    const std::array<std::byte, 4> first{std::byte{0x03}, std::byte{0x02}, std::byte{0x23}, std::byte{0x07}};
    const std::array<std::byte, 4> second{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}};
    assert(Storage::Blob::WriteFile(path, first));
    assert(Storage::Blob::WriteFile(path, second)); // Existing cache entries are replaced on Windows too.
    std::vector<std::uint8_t> loaded;
    assert(Storage::Blob::ReadFile(path, loaded));
    assert(loaded.size() == second.size());
    assert(std::equal(loaded.begin(), loaded.end(), reinterpret_cast<const std::uint8_t*>(second.data())));
    assert(!Storage::Blob::ReadFile(root / "missing.spv", loaded));
    assert(loaded.empty());
    assert(Storage::Blob::WriteFile(path, std::span{valid}.first(7)));
    assert(!Storage::Blob::ReadFile(path, words));
    assert(words.empty());
    assert(!Storage::Blob::WriteFile(path, {}));
    assert(Storage::Blob::ReadFile(path, loaded)); // A rejected write does not damage the old blob.
    assert(loaded.size() == 7);
    const auto failed = root / "blocked.spv";
    std::filesystem::create_directory(failed);
    assert(!Storage::Blob::WriteFile(failed, first));
    assert(std::filesystem::is_directory(failed));
    assert(!std::filesystem::exists(root / "blocked.spv.tmp"));
    std::filesystem::remove_all(root);
    // A module header plus one complete OpNop, then the same data cut inside an instruction.
    const std::vector<std::uint32_t> spv{0x07230203, 0x00010000, 0, 1, 0, 0x00010000};
    assert(Storage::Blob::IsCompleteSpirv(spv));
    auto malformed = spv;
    malformed[5] = 0x00020000;
    assert(!Storage::Blob::IsCompleteSpirv(malformed));
    malformed[5] = 0;
    assert(!Storage::Blob::IsCompleteSpirv(malformed));
    assert(!Storage::Blob::IsCompleteSpirv(std::span{spv}.first(4)));
    malformed = spv;
    malformed[0] = 0;
    assert(!Storage::Blob::IsCompleteSpirv(malformed));
    std::cout << "cache blob validation passed\n";
}
