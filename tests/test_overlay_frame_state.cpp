// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "gpu/shim/overlay_frame_state.h"

int main() {
    using State = BbOverlay::FrameState;
    State state;
    auto result = state.Update(false, false, 0);
    assert(!result.draw && result.change == State::Change::None);
    result = state.Update(true, false, 1000);
    assert(result.draw && result.loading && result.change == State::Change::LoadingShown);
    result = state.Update(true, true, 2000);
    assert(result.change == State::Change::None);
    // A resumed game needs no overlay. The transition still happens now, not on a later hotkey.
    result = state.Update(false, false, 2500);
    assert(!result.draw && !result.loading);
    assert(result.change == State::Change::LoadingHidden && result.duration_ms == 1500);
    result = state.Update(false, false, 5000);
    assert(!result.draw && result.change == State::Change::None);
    result = state.Update(false, true, 161000); // F7 toast
    assert(result.draw && result.change == State::Change::None && result.duration_ms == 0);
    // A later stall has its own duration; dialogs may suppress the loading overlay.
    result = state.Update(true, false, 170000);
    assert(result.change == State::Change::LoadingShown);
    result = state.Update(false, true, 171000);
    assert(result.draw && result.change == State::Change::LoadingHidden);
    assert(result.duration_ms == 1000);
    std::puts("overlay state: loading dismissal without UI/hotkeys and repeated stalls OK");
}
