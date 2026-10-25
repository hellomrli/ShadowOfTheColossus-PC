/* SPDX-License-Identifier: GPL-2.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include "../src/win_unwind.h"
#include <stdio.h>

static __attribute__((noinline)) int unwind_leaf(void) {
    CONTEXT captured;
    RtlCaptureContext(&captured);
    const DWORD64 expected = (DWORD64)(uintptr_t)__builtin_return_address(0);
    unsigned frames = 0;
    int caller_found = 0;
    for (; frames < 32 && sotc_win_unwind_step(&captured); ++frames) {
        if (captured.Rip == expected) caller_found = 1;
    }
    if (frames < 3 || !caller_found) {
        fprintf(stderr, "Windows native unwind stopped after %u frames (caller=%d)\n", frames, caller_found);
        return 0;
    }
    printf("Windows native unwind passed (%u frames; frame pointers omitted)\n", frames);
    return 1;
}
static __attribute__((noinline)) int unwind_middle(void) {
    volatile int value = unwind_leaf();
    return value;
}
static __attribute__((noinline)) int unwind_outer(void) {
    volatile int value = unwind_middle();
    return value;
}

int main(void) {
    CONTEXT invalid = {0};
    invalid.Rip = 1;
    if (sotc_win_unwind_step(&invalid)) return 1;
    return unwind_outer() ? 0 : 1;
}
