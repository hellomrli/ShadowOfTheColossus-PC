# Crash 0x490a0 (user log, 2026-10-25)

Diagnosis notes from a player's Windows run (RTX 3070, CUSA08804 folder, v0.60.6 + DLSS
build). Kept here because the fix needs evidence that was not in the log.

## What the log shows

```
00:55:36.986 [GAME] Loading a save or a new area (unused textures dropped)
00:55:37.099 [TEXTURE] Texture cleanup (level 2 of 2): 693 dropped, video memory in use 3719 MB
Fault: read access to ffffffffffffffff at guest offset 0x490a0 (thread 27248)
```

Registers: `rcx=0`, `rdx=0x10`, `rdi=0x124560fd20` (the address of the world heap the port
guards: `SOTC heap guard: heap 0x124560fd20 guarded in 0x1042700010..0x1073aaeac8`),
`rax=rbx=0x6727e2b9474242ba`.

Guest stack: `#0 0x48d70`, `#1 0x47eed`, `#2 0x128ff`, `#3 0x8bf0b`, `#4 0x18c7eb`
(all inside the guest executable; no host frame in between).

Right before it, the log repeats `page_manager.cpp:704 Protect: Not protecting guest stack
pages` and `ForEachNonStackRange: Not writing GPU data back over guest stack pages`, and the
BPE heap guard reports `kept 0x28 bytes of heap metadata`.

## Why it is not the 0.60.6 `0x49500` crash

0.60.6 removed the speculative GPU readbacks (`SOTC_RB_HOT`) that wrote over memory the game
had reused after a load, and its crash was at `0x49500`. This one is at `0x490a0`, after a
*successful* texture collection, with a `-1` base pointer rather than a stale GPU write. They
may share a root cause (guest memory reused while the port still holds a pointer into it), but
the addresses and the timing differ, so it needs its own evidence.

## Candidates, in order

1. **World heap guard leaving a stale block.** `rdi` points at the guarded heap and the crash
   happens during the load that reshapes it. The guard keeps 0x28 bytes of heap metadata and
   skips pieces; a freed/reused block could keep a `-1` next pointer. Check
   `sotc_heap_guard` (SOTC_HEAP_GUARD=0 turns it off) as the first A/B.
2. **Texture collection deleting an image the loader still uses.** The collection runs 124 ms
   before the fault; a stale `ImageId`/view would crash in the loader, not in the renderer.
3. **A guest race in the loader itself** (the game's own streaming threads) that the port makes
   more likely by stalling the command processor during the collection.

## Evidence needed

- `user/crashes/*.dmp` from the run (the log names it).
- `user/events.log` around the crash (the last 40 lines).
- A reproduction: load a save / enter a new area with `SOTC_HEAP_GUARD=0`, then with
  `SOTC_LOAD_FLUSH=0`, and note which one survives.
- `SOTC_PROFILE=GpuCommandProcessor` is not useful here: the crash is on a guest thread, so a
  `SOTC_PROFILE` of the faulting thread (the loader's) is the one to capture.

## What is already ruled out

- The `SRT walker read zero` warnings are the walker's fault handler serving a clean read; they
  are counted and do not stop the walker. They appear in runs that do not crash.
- `page_manager` `Protect` messages are the port deliberately skipping guest stack pages; they
  are warnings about a range it must not protect, not the fault address.
