# Upstream shadPS4 review after b4e7ae7

The port vendors shadPS4 at `b4e7ae776071ceb1dd35004577a8719481e1d258` (2026-10-02). This file
tracks what was reviewed from `b4e7ae7..12a45645` (2026-10-24, 98 commits touching
`shader_recompiler`/`video_core`) and what was actually taken.

## Taken (branch `next/upstream-backports`)

| Upstream | What | Why |
|---|---|---|
| `524be7c1` | Exclude `GetUserData` from phi folding | User data is a runtime value; folding it into a constant breaks shaders that read different user data on different paths. SotC's SRT walkers read user data heavily. |
| `78bedfac` | Preserve CFG fallthrough edges (`block.cond = True`, skip break/return/unreachable in `TryFindForwardBlock`, `AddBranch` on the next code block) | SotC already had the `block.cond` line but not the other two. Without them a fallthrough edge could be dropped, producing a shader that skips code. |
| `f0ea8eb7` | `S_GETREG_B32` / `HW_ID.SE_ID` as virtual shader engine 0 | The opcode was known but had no translator: any shader reading the shader-engine id hit `UNREACHABLE`. SotC's SRT walkers and culling shaders are exactly the kind of code that reads it. |
| `c34ca21f` | Preserve EXEC scope after an empty branch | One line in `SplitDivergenceScopes`: an empty scope must still open EXEC masking for the next instruction when it is an open-scope one. |
| `603cff43` | Stable phi deduplication order | The worklist was built from `unordered_map` iteration order, so the same shader could produce different IR between runs. |
| `1c2b8f97` | Proper `V_RCP_CLAMP_F32`, `V_RSQ_CLAMP_F32`, `V_RSQ_LEGACY_F32`, `V_LOG_CLAMP_F32` | SotC mapped all four to the non-clamped instructions, so results could be `inf`/`NaN` where GCN clamps. (The `V_CVT_PK_U8_F32` part was already backported.) |
| `b08fd9bb` | Detect fp64 use from the instruction types | Only `PackDouble2x32`/`UnpackDouble2x32` were checked; a shader whose first fp64 instruction was e.g. `FPAdd64` could be compiled without fp64 support. |
| `6752d8b1` | `BUFFER_WBINVL1` / `BUFFER_WBINVL1_SC` | The format table described them as 4-operand instructions and the translator had no case, so a shader using a cache invalidation hit `UNREACHABLE`. They take no operands and lower to the device memory barrier. |

`ShaderBinaryVersion` 33 → 37 (all of the above change shader output; the cache is rebuilt on
first start). Also SotC-local: `Instance::~Instance` now calls `Dlss::Shutdown()` before the
device is destroyed (the `atexit` handler used to run after it).

## Already present in SotC (no action)

| Upstream | Where |
|---|---|
| `c01ae7e6` page_manager fault probe limited to the faulting page | `page_manager.cpp` `HandleGuestFault`: `size = min(8, GetNextPageAddr(addr) - addr)`. |
| `eedf851a` texture cache drops images whose untracked edges the guest overwrote | `texture_cache.cpp` marks `MaybeCpuDirty` in `UntrackImageHead/Tail`; `SafeToDownload()` refuses dirty images, and `RefreshImage` hashes the first texels. Same intent, delayed hashing. |
| `78bedfac` first hunk (`block.cond`) | `control_flow_graph.cpp:362`. |
| `bcf083f0` clamp tracked page end to 40 bits | `page_manager.cpp` `EnsurePages`: `end = min(end, 1 << 40) - 1`. |
| `fb6367d2` 32-bit literals in high dword, `dd43d1ca` `V_CVT_PK_U8_F32`, `94e21778` `V_BFM_B32`, `ef08b1e1` `V_ALIGNBIT`, `d1cb90db` `ADDC`, `2335629` `V_INTERP_MOV_F32`, `d96278e` compute FP mode, `e518a65` `S_SUBB_U32` | Listed in `gpu/VENDOR.txt` (backported before this review). |

## Candidates left for later (not taken now)

These are real fixes but either touch code SotC has heavily rewritten or change behavior that
cannot be verified without a Windows run. They are listed so the next pass does not re-review
them:

- `f0ea8eb7` `HW_ID.SE_ID` reads; `6752d8b1` buffer cache invalidation instructions;
  `08225c2b` LDS barrier singleton dimensions; `c1b26558` `has_derivatives` with
  `CoarseDerivative`; `603cff43` deterministic phi dedup; `c34ca21f` EXEC scope after an empty
  branch; `b08fd9bb` fp64 usage detection; `1c2b8f97` RCP/RSQ/LOG clamp variants.
- Texture/depth: `7e778987` tiled mip layout, `fecfbed0` color/stencil association,
  `626f3ace` stencil-only depth targets, `92c7a884` D32↔D32S8 copies, `b3d46e96` mip copies.
- Large refactors, deliberately not merged: `e6faa2e3` garbage collector part 2, `8e23388a`
  renderer optimizations, `4ddaada8` num buffers, `fca10b26` remove batching.

## Rule for taking an upstream fix

1. Apply only the commit's own hunks; do not pull surrounding refactors.
2. Keep SotC's `sotc:` comments and env switches intact.
3. If shader output changes, bump `ShaderBinaryVersion` (the cache is rebuilt on first start).
4. Build through `.github/workflows/build-windows.yml` before merging to `main`; a runtime change
   also needs a Windows run from the player.
