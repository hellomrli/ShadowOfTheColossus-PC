// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: publish only a complete cache blob, replacing an older blob atomically on Windows too.
#include "video_core/cache_blob.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Storage::Blob {

bool WriteFile(const std::filesystem::path& path, std::span<const std::byte> data) {
    if (data.empty() || data.size() > MaxBytes) {
        return false;
    }
    auto temporary = path;
    temporary += ".tmp";
    bool complete = false;
    {
        std::ofstream file{temporary, std::ios::binary | std::ios::trunc};
        if (file) {
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
            file.flush();
            complete = static_cast<bool>(file);
            file.close();
            complete = complete && static_cast<bool>(file);
        }
    }
    std::error_code error;
    if (complete) {
#ifdef _WIN32
        complete = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
        std::filesystem::rename(temporary, path, error);
        complete = !error;
#endif
    }
    if (!complete) {
        std::filesystem::remove(temporary, error);
    }
    return complete;
}

} // namespace Storage::Blob
