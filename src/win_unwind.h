/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SOTC_WIN_UNWIND_H
#define SOTC_WIN_UNWIND_H
#include <windows.h>
#include <stdint.h>

/* Unwind only native PE frames. The caller must guard RtlVirtualUnwind against faults in
 * malformed contexts/unwind data; guest/JIT frames have no trustworthy Windows unwind table. */
static inline int sotc_win_unwind_step(CONTEXT *context) {
    MEMORY_BASIC_INFORMATION memory;
    if (!context->Rip || !VirtualQuery((const void *)(uintptr_t)context->Rip, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
        !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    const DWORD64 old_sp = context->Rsp;
    DWORD64 base = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(context->Rip, &base, NULL);
    if (fn) {
        PVOID handler_data = NULL;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context->Rip, fn, context,
                        &handler_data, &establisher, NULL);
    } else {
        DWORD64 ret = 0;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(uintptr_t)old_sp, &ret, sizeof(ret), &got) ||
            got != sizeof(ret))
            return 0;
        context->Rip = ret;
        context->Rsp += sizeof(ret);
    }
    return context->Rip && context->Rsp > old_sp && context->Rsp - old_sp <= 1024 * 1024;
}
#endif
