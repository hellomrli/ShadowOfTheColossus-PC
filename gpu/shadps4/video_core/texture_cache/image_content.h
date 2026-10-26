// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: shared image-content state and upstream #5444 expansion policy, without a GPU dependency.
#pragma once

#include "common/enum.h"

namespace VideoCore {

enum ImageFlagBits : u32 {
    Empty = 0,
    MaybeCpuDirty = 1 << 0, ///< The page this image is in was touched before the image address
    CpuDirty = 1 << 1,      ///< Contents have been modified from the CPU
    GpuDirty = 1 << 2, ///< Contents have been modified from the GPU (valid data in buffer cache)
    Dirty = MaybeCpuDirty | CpuDirty | GpuDirty,
    GpuModified = 1 << 3, ///< Contents have been modified from the GPU
    Registered = 1 << 6,  ///< True when the image is registered
    Picked = 1 << 7,      ///< Temporary flag to mark the image as picked
};
DECLARE_ENUM_FLAG_OPERATORS(ImageFlagBits)

constexpr bool HasValidGpuContents(ImageFlagBits flags) {
    return True(flags & ImageFlagBits::GpuModified) && False(flags & ImageFlagBits::Dirty);
}

template <class Refresh, class Copy>
void RefreshExpandedContents(ImageFlagBits source_flags, Refresh&& refresh, Copy&& copy) {
    // Capture validity before refresh; a stale copy must not overwrite the new guest upload.
    const bool preserve_gpu = HasValidGpuContents(source_flags);
    refresh();
    if (preserve_gpu) {
        copy();
    }
}

} // namespace VideoCore
