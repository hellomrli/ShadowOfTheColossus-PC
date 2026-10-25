// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: loading transitions must be observed even on frames that need no overlay draw.
#pragma once

#include <algorithm>
#include <cstdint>

namespace BbOverlay {

class FrameState {
public:
    enum class Change { None, LoadingShown, LoadingHidden };
    struct Result {
        bool draw;
        bool loading;
        Change change;
        std::int64_t duration_ms;
    };

    Result Update(bool loading, bool other_overlay, std::int64_t now_ms) {
        Change change = Change::None;
        std::int64_t duration = 0;
        if (loading != was_loading) {
            was_loading = loading;
            if (loading) {
                loading_since = now_ms;
                change = Change::LoadingShown;
            } else {
                duration = std::max<std::int64_t>(now_ms - loading_since, 0);
                change = Change::LoadingHidden;
            }
        }
        return {loading || other_overlay, loading, change, duration};
    }

private:
    bool was_loading{};
    std::int64_t loading_since{};
};

} // namespace BbOverlay
