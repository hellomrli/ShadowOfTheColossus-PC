// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: cache data is optional. Reject incomplete/oversized blobs before the driver sees them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <type_traits>
#include <vector>

namespace Storage::Blob {

// A single shader or pipeline metadata blob never needs an unbounded archive allocation.
inline constexpr std::uint64_t MaxBytes = 64 * 1024 * 1024;

template <typename T, typename Reader>
bool Read(std::vector<T>& output, std::uint64_t bytes, Reader&& read) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.clear();
    if (!bytes || bytes > MaxBytes || bytes % sizeof(T) != 0) {
        return false;
    }
    std::vector<T> complete(static_cast<std::size_t>(bytes / sizeof(T)));
    if (!read(std::as_writable_bytes(std::span{complete}))) {
        return false;
    }
    output.swap(complete);
    return true;
}

template <typename T>
bool ReadFile(const std::filesystem::path& path, std::vector<T>& output) {
    output.clear();
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return false;
    }
    const auto end = file.tellg();
    if (end < 0 || !file.seekg(0)) {
        return false;
    }
    return Read(output, static_cast<std::uint64_t>(end), [&](std::span<std::byte> data) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        const bool complete = static_cast<bool>(file);
        const auto extra = file.peek();
        return complete && extra == std::ifstream::traits_type::eof() && !file.bad();
    });
}

bool WriteFile(const std::filesystem::path& path, std::span<const std::byte> data);

// Structural completeness only, not semantic shader validation or a replacement for spirv-val.
inline bool IsCompleteSpirv(std::span<const std::uint32_t> words) {
    if (words.size() < 5 || words[0] != 0x07230203 || !words[3] || words[4] != 0) {
        return false;
    }
    for (std::size_t offset = 5; offset < words.size();) {
        const auto count = words[offset] >> 16;
        if (!count || count > words.size() - offset) {
            return false;
        }
        offset += count;
    }
    return true;
}

} // namespace Storage::Blob
