# DLSS Super Resolution in SotC-PC

Optional NVIDIA DLSS (DLAA and Super Resolution) that replaces the FSR 1 pass when the bridge
DLLs are next to `bb-probe.exe`. No NVIDIA code is linked into the port: NGX is loaded at run
time and every failure falls back to FSR 1.

## Pieces

| Where | What |
|---|---|
| `gpu/dlss_bridge/` | `sotc_dlss.dll`, MIT, MSVC-only, built separately (the only part that includes `nvsdk_ngx_*.h`). ABI 1 is Super Resolution only; SotC uses Streamline for DLSS-G. |
| `vk_dlss.cpp/.h` | Loads the bridge + `nvngx_dlss.dll` from the executable's folder, negotiates NGX's Vulkan instance/device extensions, creates the feature and evaluates it. `SOTC_DLSS=0` returns null and nothing else changes. |
| `host_passes/dlss_upscaler.cpp` | Converts the presented image to RGBA16F and the depth to R32F, records the evaluation, returns an RGBA16F view the existing post-process pass draws to the swapchain. |
| `vk_dlssg.cpp` | Already captured the G-buffer depth, motion vectors, camera jitter and reset for DLSS-G; `Upscaling()`/`GetUpscaleInputs()` expose the same data to DLSS SR, and `Capturing()` keeps capturing when only SR is on. |
| `vk_presenter.cpp` | Before the FSR 1 call: if DLSS ran, its view replaces the FSR view; otherwise FSR 1 runs exactly as before. |

## Data flow (presentation stage)

```
game image (render size, R8G8B8A8Srgb / B8G8R8A8Srgb / A2R10G10B10)
        │  dlss_color.comp: sample through a UNORM or sRGB view, store RGBA16F
        ▼
   colour RGBA16F ─┐
   depth  R32F    ─┼─► DLSS feature (NGX) ─► RGBA16F output (presentation size)
   motion R16G16  ─┤                              │
   jitter/reset   ─┘                              ▼
                                            pp_pass → swapchain
```

- **Colour space:** DLSS's SDR mode expects display-referred (sRGB) values. An sRGB-format game
  image is decoded by the sampler and re-encoded by the shader; an A2R10G10B10 image is sampled
  through a UNORM view and copied unchanged. The post-process pass then treats the DLSS output
  as sRGB-encoded (`srgb_input = 1`), which is the same round trip the 10-bit FSR path already
  used.
- **Depth:** the game's buffer is reversed-Z. The shader stores `1 - d` (0 at the near plane) in
  R32F, so the feature is created with `depth_inverted = false`.
- **Motion vectors:** the G-buffer stores `curUV - prevUV`, so `mv_scale_x/y` are the render
  width/height. The feature is created with `MVLowRes`.
- **Jitter:** the game's own TAA jitter in pixels, the same value DLSS-G gets.
- **Reset:** camera cuts from the game constants, the first frame after the feature is created,
  and the first frame after DLSS was skipped (missing inputs, HDR or a different upscaler).

## Settings

| Variable / UI | Effect |
|---|---|
| `SOTC_DLSS=0` | Off; FSR 1 is used. |
| `SOTC_DLSS=1` | On when the DLLs and an RTX card are present (the default). |
| `SOTC_UPSCALER=dlss/fsr1/bilinear` | Startup selection, shared with the launcher and in-game menu. FSR 1/bilinear selection does not disable NGX, so DLSS can still be selected live. |
| `SOTC_RENDER_RES=WxH` | The game renders smaller; `SOTC_OUTPUT_RES=WxH` keeps the presentation size — DLSS upscales between them. |
| Menu → Quick settings → Upscaler | DLSS / FSR 1 / Bilinear, applied at the next frame. |
| Menu → Nitidez (RCAS) | Also sets DLSS sharpness (0..1); unlike RCAS attenuation, larger values mean stronger sharpening. |

New launcher settings default to `dlss`; existing `fsr1`/`bilinear` settings are not migrated.
Explicit DLSS selection always leaves FSR 1 enabled as its fallback. For older scripts without
`SOTC_UPSCALER`, an explicit `BB_FSR1=0` still selects bilinear; otherwise DLSS is preferred.
`SOTC_DLSS=0` is a hard disable for that session, not the launcher's mode selector.

## Fallbacks (all print `SOTC DLSS:` once and keep FSR 1)

- DLLs missing, non-NVIDIA GPU, NGX init or capability failure, required extensions missing.
- A frame without a G-buffer draw (menu, movie, loading) has no depth/motion: no inputs, FSR 1.
- Feature creation/evaluation failure.
- Render or output size changes after the feature exists: rebuilding NGX's feature while the GPU
  may still read it is unsafe, so DLSS stays off until a restart. Changing `SOTC_RENDER_RES` or
  `SOTC_OUTPUT_RES` therefore needs a restart.
- HDR output keeps FSR 1 (the 10-bit buffer is PQ encoded; DLSS would need linear values).

## Verifying a run

1. `SOTC DLSS: ready` in `user/last_run.log` (or stderr) — the DLLs loaded, NGX initialized.
2. `SOTC DLSS: feature creation failed` / `evaluation failed` — NGX rejected the parameters; the
   game keeps running on FSR 1.
3. At native size the image should be DLAA: slightly cleaner edges than FSR 1, HUD included (the
   whole frame is upscaled, as with FSR 1).
4. With `SOTC_RENDER_RES=1280x720` + `SOTC_OUTPUT_RES=1920x1080` the image should be sharper than
   bilinear and less aliased than 720p; `SOTC DLSS: DLSS 1280x720 -> 1920x1080, quality 1` in
   the log.
5. Exit the game: no crash after the swapchain is destroyed (NGX is shut down in
   `Instance::~Instance`, before the device).
6. Select FSR 1 or Bilinear in the launcher, then DLSS in F1: switching should work without a
   restart. Close and reopen the launcher: the saved choice must still be selected.
7. With DLSS selected, menus/movies and `SOTC_DLSS=0` must keep the FSR 1 fallback, not silently
   disable it. Check High/Medium/Low/Off sharpening in that order; the DLSS parameter must
   decrease rather than increase. Visual verification still needs an RTX Windows run.

## Building

CI builds the bridge with MSVC (`.github/workflows/build-windows.yml`) and copies
`out/dlss-bridge/sotc_dlss.dll` + NVIDIA's `nvngx_dlss.dll` into the package. Locally:

```bash
cmake -S gpu/dlss_bridge -B out/dlss-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSOTC_DLSS_SDK_ROOT=<NVIDIA/DLSS checkout> \
      -DVULKAN_INCLUDE=<Vulkan-Headers include>
cmake --build out/dlss-bridge
```

The bridge is deliberately outside the main CMake project: `bbgpu` is built with Clang and must
never see the MSVC-only NGX libraries.
