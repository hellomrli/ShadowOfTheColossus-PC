# Shadow of the Colossus — PC native port · v0.68 (preview)

[Releases](../../releases) · [Changelog](CHANGELOG.md)

**v0.68 is a prerelease of this fork, focused on preparation diagnostics and patch safety.**
It reports actual executable signatures, distinguishes content-region metadata from code
compatibility, checks every requested FPS patch site and counts the real base/update resources.
It keeps 0.67's stability/CPU fixes. FSR 1 + FSR 3 settings, DLSS and the shader cache are unchanged;
these diagnostics do not establish support for another executable variant or validate gameplay.
Download previews manually from Releases: the launcher's updater follows this fork's stable
releases, not previews or upstream releases. Keep a backup of `user/` and `sotc.ini` when moving
to a new directory.

**A v0.68 e uma pre-release deste fork, com diagnostico da preparacao e patches mais seguros.**
Confere os bytes de cada patch de FPS e mostra assinaturas, regiao informativa e recursos da
base/atualizacao no relatorio. Mantem as correcoes de estabilidade/CPU da 0.67, FSR 1 + FSR 3,
DLSS e as protecoes locais. Nao confirma suporte a outro executavel nem estabilidade do jogo.
[Diagnostico](#preparation-diagnostics), [plano de estabilidade/CPU](docs/sotc/STABILITY-CPU-2026-10-25.md)
e [revisao do upstream](docs/sotc/UPSTREAM-REVIEW-2026-10-26.md).

> **No game or Sony files are included.** You need your own game dump and, for sound, two
> modules from your own console. This project is not affiliated with Sony Interactive
> Entertainment or Bluepoint Games.

---

**Status: the game can be finished from start to end** (v0.60.3: played through the last
colossus, the ending and the credits, with high quality textures, PS4 Pro mode and 60 FPS on).

A native Windows port of the PS4 **Shadow of the Colossus** (Bluepoint, 2018; CUSA08809, EU 1.01)
for **Windows 10/11**. The game's own x86-64 code runs directly on the PC's CPU and the PS4 GPU
is translated to Vulkan. It is not a full emulator: it is a loader with the system functions the
game needs, in the style of [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc).

### What's new

| Version | Improvements and fixes |
|---|---|
| **0.68 (preview)** | **Preparation diagnostics and patch safety:** expected/observed executable signatures, informational content-region hints, real base/update resource inventory and per-site original-byte checks for FPS patches. No new executable-variant whitelist or renderer changes. |
| **0.67 (preview)** | Three focused shadPS4 backports: **CE/DE wait guards** (#5406), **no stale texture overwrite during expansion** (#5444), and **reused Windows high-resolution timers** (#5435) with failure fallback and APC reentry protection. No shader-cache, DLSS/FSR 3 or memory-protection changes. |
| **0.66 (preview)** | **Event-driven idle recorder**, chunk-boundary wakeup and final-chunk cleanup; **actual process/thread CPU telemetry**, immediate loading-dismissal logs and atomic dialog flags. No-GPU wakeup, lifetime and UI-state tests. Stability/CPU first; no new DLSS features or shader-cache changes. |
| **0.65 (preview)** | **Real Vulkan command buffers for DLSS**, with ordered creation/evaluation on the recording thread and synchronous fallback results; NGX crash-context labels, balanced debug labels on failure and a no-GPU regression using the production recorder. A later RTX 3070 run created DLAA and exited normally; no renderer-default or shader-cache changes. |
| **0.64 (preview)** | **Verify the actual executable before fixed-address CPU patches**, including update selection in the launcher; reject malformed/truncated cache blobs and publish only complete writes; **native Windows crash stacks and active Vulkan operation/shader context**. RTX 3070 driver-crash investigation remains open; no shader-output or renderer-default changes. |
| **0.63 (preview)** | **Safer self-updates** with protected saves/settings and retained recovery backups; persistent **DLSS / FSR 1 / Bilinear** selection in both menus, corrected DLSS sharpening and history reset; reliable preparation cache/update overlays; **complete DLL dependencies for PCs without MSYS2**, clean-checkout packaging and Linux/Windows regression checks. |
| **0.62 (preview)** | **DLSS in the F1 menu**, ten upstream shader/recompiler fixes, and NGX shutdown before Vulkan device destruction. Shader cache version 38: upgrading from 0.61 rebuilds the cache on first start. |
| **0.61** | **DLSS Super Resolution (RTX 20 and newer)**: the package now ships this project's MIT `sotc_dlss.dll` bridge and NVIDIA's `nvngx_dlss.dll`; with an RTX card DLSS replaces FSR 1 automatically — **DLAA** at native size, **Super Resolution** when the game renders below the output size. Windows builds are produced by GitHub Actions. |
| **0.60.6** | **No more rainbow garbage on textures and far fewer "closed out of nowhere" crashes** (the cause: speculative GPU readbacks written over memory the game had reused, after loading a save or a new area), **no more black dots on sandy ground**, **in-game menu cleaned up** (FPS, upscaler and sharpening now change at once; Bloodborne leftovers removed), **hard disk read-ahead** (fewer stalls in new areas on HDDs), textures refreshed on every load, a readable session event log (`user/events.log`). |
| **0.60.5** | **Texture quality selector: Off / High / Maximum** (#235: sharp again), **memory leak fixed** (~60 MB/min, long-session stutter), **+3% average and +12% minimum FPS** (descriptor binds reused), **loading after a GPU driver update: 106 s → 7 s**, possible fix for **white characters on AMD GCN** (RX 470–590, Vega), **4-thread CPUs** no longer stuck loading, a hang watchdog, short hitches no longer flash a black loading screen, code review fixes. |
| **0.60.4** | **No more cyan blocks on stone or blinking flowers** with high quality textures (the 0.60.3 crash protection switched the texture detail every second or two; now with hysteresis). |
| **0.60.3** | **The game can be finished from start to end** (played from the last colossus through the ending and credits): fix for the high quality textures crash in wide views (#186), fix for the crash on the load after the credits, +2.7% FPS at the Shrine. |
| **0.60.2** | **Quick settings in the in-game menu** (60 FPS, frame generation, high quality textures, far shadows, volume: apply at once and are kept), **the mouse works in the menu**, **animated loading screen while the shaders are prepared** at start. |
| **0.60.1** | **In-game menu** (Insert / F1 / L3+R3: FPS, GPU and VRAM, fullscreen, resolution, sharpening, mouse camera, effects), **Bloodborne leftovers removed** (~11,700 lines), the game exe has the SotC icon. |
| **0.60** | **Textures on GTX 10xx**, no title-screen hang, **starts on low-memory PCs**, **60 FPS through the game's own Performance mode** (right physics), **each copy has its own `user` folder**, +6% FPS at the Shrine, FSR 3 without black tiles on RX 7000. |
| **0.59.1** | **Far shadows no longer blink** (AMD and NVIDIA; new launcher option), **AMD stripes** on shadows and water fixed, Intel fixes, the log shows the GPU and driver. |
| **0.59** | **More FPS** (Oussama268's fork: draw thread, shaders translated in parallel; bridge 30 → 37–40 FPS), **fix for the ending and save-load crash** (still being tested: report in the issues), **F11 fullscreen**, **video memory use** and **volume** options, launcher **self-update**, ultrawide with PS4 Pro mode, fixes for 6 GB cards and Chinese Windows. |
| **0.58.1** | **High quality textures** (sharp leaves and rocks; the game had not streamed its high resolution versions since 0.0.13) and the **game window's own icon** (or the game's, or a picture of yours). |
| **0.58** | **Community release** (Lucas-blip-png, Gabriell-os, Oussama268, jownatan, xeodeo, nakaionyt/y7ngboy, tabnul): **fewer crashes** (out of video memory, game freezing, post-colossus crash in PS4 Pro mode), **+15% FPS** and less video memory, AMD fixes (RDNA3/4 black tiles), **21:9/32:9 widescreen**, **FPS 30/60/unlimited**, **Xbox button icons**, FSR 3 / DLSS-G frame generation (experimental), launcher in Spanish, Chinese, Korean and Russian. |
| **0.57** | **AMD cards:** fix for the green/pink fog and characters (AMD now uses the NVIDIA 32-lane path; diagnosis from issue #7, not yet tested on an AMD card). **New launcher option** to turn off direct memory access: ~10% more FPS, flatter distant water. |
| **0.56** | **Open-world water with waves:** lakes and rivers below the waterfalls are no longer flat as a mirror (a shader instruction, `S_WQM_B64`, was ignored). **The upper lake ripples and reflects far away** (direct memory access on by default, as in Pink-shadPS4). Distant trees without "colored garbage". |
| **0.0.13** | **No more crashes:** fixed the 0.0.12 crash in the intro/menu (the game asked for more textures than its pool holds). Tested ~40 min across the map with two save loads. **Lake reflections** over almost the whole surface, with ripples (a shader loop guard cut the reflection rays short). |
| **0.0.12** | **Sharp textures back, without crashes:** fixed the memory corruption that closed the game after loading a save, and texture streaming is back (much sharper rocks and walls). **Water:** splashes, rings and foam while swimming; the lake reflects the sky and the sun's glint; environment reflections use each probe's correct cubemap (fix from Pink-shadPS4). |
| **0.0.11** | Fixed the "colored garbage" grass texture of 0.0.10. |

Full details in the [CHANGELOG](CHANGELOG.md) (in Portuguese).

### In-game menu and shortcuts

Press **F1** (or **Insert**, or **L3+R3** on the controller) while playing. The mouse is free while
the menu is open; **Esc** or F1 closes it.

![In-game menu (F1)](docs/images/sotc-menu-f1-en.png)

- **Quick settings (apply at once):** FPS (30 / 60 / unlimited), upscaler (DLSS when available, FSR 1 / bilinear),
  sharpening (RCAS), frame generation, **texture quality**, far shadow reuse, volume. They are
  also saved as the launcher's choice for the next start.
- **Display:** fullscreen, FPS counter.
- **Camera & mouse:** mouse camera on/off, sensitivity, invert Y.
- The header shows FPS, frame time, the GPU and its video memory in use. Output resolution,
  effects (fog, vignette, SSAO), 16x AF, PS4 Pro mode and video memory use stay in the launcher
  (they apply when the game starts).

| Key | What it does |
|---|---|
| **F1** / Insert / L3+R3 | Opens or closes the in-game menu |
| **F7** | 30 FPS cap on/off (60 FPS without the cap) |
| **F8** | Frame generation: off → DLSS-G → FSR 3 (when the GPU supports it) |
| **F10** | Performance overlay (FPS and frame time) |
| **F11** | Window / fullscreen |

### DLSS Super Resolution (RTX 20 and newer)

The Windows package ships `sotc_dlss.dll` (this project's MIT bridge, source in
`gpu/dlss_bridge`) and NVIDIA's `nvngx_dlss.dll`. With an NVIDIA RTX card and both DLLs next to
`bb-probe.exe`, DLSS replaces FSR 1 automatically:

| Situation | What runs |
|---|---|
| render size = output size (1080p → 1080p) | **DLAA**, the same-size anti-aliasing mode |
| render size below the output size (`SOTC_RENDER_RES=1280x720` with a 1080p output) | **DLSS Super Resolution**; the quality mode follows the ratio |

`SOTC_DLSS=0` turns it off (FSR 1 is used instead). HDR output keeps FSR 1 for now. Frames
without a G-buffer draw (movies, menus, loading screens) keep FSR 1 for that frame, and a camera
cut resets the DLSS history automatically. The HUD is part of the upscaled image, exactly as
with FSR 1. The first frame prints `SOTC DLSS: ready`; without the DLLs or on another GPU
everything works as before with FSR 1.

The launcher and the in-game menu both keep the **DLSS / FSR 1 / Bilinear** choice for the next
start. New installations prefer DLSS with FSR 1 fallback; an existing FSR 1 or Bilinear choice
is preserved. `SOTC_UPSCALER=fsr1` or `SOTC_UPSCALER=bilinear` selects a different mode without
disabling the DLSS bridge, so it can still be selected in the in-game menu. `SOTC_DLSS=0`
disables the bridge for the whole session. Returning to DLSS after skipped frames resets its
history.

To render below the output size, put both lines in `sotc.ini` (or the environment):

```ini
SOTC_RENDER_RES=1280x720
SOTC_OUTPUT_RES=1920x1080
```

The first is the game's render size (fewer pixels to shade, which is where the speed comes
from), the second the size actually presented; DLSS upscales between them.

### Texture quality

In the launcher (**Quick settings → Textures**) and in the in-game menu (changes at once):

| Level | What it does | Possible problems |
|---|---|---|
| **Off** | The game's 256×256 versions only (the behaviour before 0.58.1). | Blurry ground, rocks and leaves. For GPUs short of video memory. |
| **High (stable)** — default | The game streams its high resolution textures. When the game's texture memory runs low, the textures ask for a coarser level so the game frees space before it runs out. | A little less sharp in large, wide areas (the game's texture memory is almost always full there, so it stays on the coarser level; #235). |
| **Maximum (sharp)** | High resolution textures always, without that protection. | It **may close the game in very wide views** (sword raised against the sun, the Shrine, the last colossus' area; fault `0x1a371`, #186), mostly in **PS4 Pro mode**. Not seen in our 5–10 minute runs, but reported before. If it happens, go back to **High**. Uses more video memory. |

### Known issues

- **RTX 3070 startup crash in `nvoglv64.dll+0xe35140`:** reported both with FSR 3 and with frame
  generation off. The 0.64 native stack identifies NGX feature creation. 0.65 fixes an internal
  placeholder command buffer escaping into NGX. The affected user subsequently reported
  successful 1080p DLAA creation and about 7.5 minutes with FSR 3, exit code 0. This is a
  successful startup sample, not full-game or image-quality validation. FSR 1 is also a
  confirmed workaround; the current focus is stability/CPU rather than new DLSS work.
  `SOTC_DLSS=0` in **Advanced → Extra variables** disables even NGX initialization;
  FSR 1 selection skips SR but still allows initialization. [Investigation and patch safety](docs/sotc/CRASH-NVIDIA-2026-10-25.md).
- The first start after updating rebuilds the shaders: the first load is slow and stutters.
- **Texture quality "Maximum"** can close the game in very wide views (sword raised against the
  sun, the last colossus' area; #186). Since 0.60.6 it is not available in PS4 Pro mode, where it
  closed within minutes.
- The first start after an update rebuilds the GPU driver's shader cache: about 2 minutes of
  loading screen, and short stalls the first time each area is visited.
- **AMD GCN** cards (RX 470–590, Vega, APUs on the legacy driver): characters and Agro may glow
  white (#152, #158, #187). 0.60.5 adds the LDS barriers these cards were missing (PR #205), which
  is the likely fix: please report whether it helps. `SOTC_AMD64_LDS=0` turns it off.
- **60 FPS:** since 0.60 the game targets 60 FPS itself (its PS4 Pro Performance mode), which
  should fix the 60 FPS physics (the 2nd colossus' wall, colossi "flying"); still being tested
  there. `SOTC_MODES=af16` in the extra variables goes back to the old mode.
- **Frame generation** (experimental): the health/stamina bars may shake (`SOTC_FG_HUDLESS=1`
  tries a HUD-less image), and it only matters at 30 FPS.
- With 8 GB of video memory or less, pick a lower **Video memory use** level if the game runs out.

### Download and use

1. Download `SotC-PC.zip` from [Releases](../../releases) and unzip it into a folder.
2. Run **`SotC.exe`**. On first start the launcher searches the PC's drives for:
   - the game folder (the CUSA08809 dump with `eboot.bin`, `sce_module` and `sce_sys`);
   - the sound modules (`libSceNgs2.sprx` and `libSceUlt.sprx`) of an installed shadPS4
     (`user/sys_modules`), which it copies to the port's data folder;
   - a shadPS4 save of the game, which it offers to import;
   - the game's DLCs, if any (shadPS4's `addcont` folder, or a folder with one subfolder per DLC).
3. Check the **Game and saves** page. The **game library** works like shadPS4's.
4. Click **PLAY**.

Requirements: Windows 10/11 64-bit and a **Vulkan 1.3** GPU (NVIDIA, AMD or Intel with a recent
driver). No Python install needed.

| What | Where from | What for |
|---|---|---|
| CUSA08809 game dump (EU 1.01 preferred) | your PS4, through GoldHEN | the game |
| decrypted `libSceNgs2.sprx`, `libSceUlt.sprx` | your PS4 (`/system/common/lib/`, GoldHEN FTP), or shadPS4's `user/sys_modules` | sound (silent without them) |

The patch modes (60 FPS, 16x filtering) only apply to a verified EU 1.01 executable; a folder
name or `param.sfo` version is not verification. Fixed-address red-zone CPU patches use the
same executable check. Other dumps are not validated by this release; do not force EU offsets
or edit metadata to bypass the check.

### Launcher features

Game library and automatic search, shadPS4 save import, 720p–4K output (the game renders at
1080p, upscaled with FSR 1 + RCAS), PS4 Pro mode, 60 FPS and 16x AF modes, controller support
(DualShock 4, DualSense, Xbox) with mouse camera, key remapping, Bluepoint engine cvars, desktop
shortcut and a session log in `user/last_run.log`.

In 60 FPS mode the title-screen attract demo averages ~67 FPS on an RTX 2060 SUPER, with
29–34 FPS in its heaviest section. Vulkan only (no DirectX). AMD and Intel can enable the
NVIDIA-only submission options under **Image and FPS > GPU work submission**.

**Why no sound files?** The two `.sprx` files are Sony firmware and may not be redistributed;
the launcher finds and copies yours automatically. **The game closed by itself?** Open an issue
with `user/last_run.log`.

---

## Building

Windows with [MSYS2](https://www.msys2.org/) (CLANG64); packages:
[packaging/windows/README.md](packaging/windows/README.md). To freeze `SotC.exe`: a Windows
Python with PyInstaller and Pillow (`WINPYTHON`).

```
bash build.sh                              # out/bb-probe.exe
bash packaging/windows/package_sotc.sh     # dist/SotC-PC.zip
```

Packaging refuses any `.sprx`/`.prx`: Sony firmware never goes into the package. Test tools:
`tools/`. Diagnostic variables: [docs/sotc/README-SotC.md](docs/sotc/README-SotC.md).

### Regression tests

```bash
python -m unittest discover -s tests -v
mkdir -p out
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I. tests/test_sotc_live.cpp -o out/sotc-live-test
out/sotc-live-test
```

The standalone C++ tests need no GPU or game dump. Python tests that need a native binary are
reported as skipped until that binary is built. CI runs the Python suite on Linux, the launcher,
updater and preparation tests on Windows, and live-settings, cache-blob and diagnostic-context
tests on both build paths. The Windows build also tests native unwinding with frame pointers
omitted. The production command recorder runs against a mock driver on Linux and Windows to
check the external-SDK handoff without a GPU or NGX. The patch-guard tests use small synthetic
executables, not game or firmware files.
Recorder tests also check event-driven wakeups and idle shutdown; separate tests cover
loading-state transitions and real OS CPU clocks/thread lifetime. See the
[stability/CPU verification guide](docs/sotc/STABILITY-CPU-2026-10-25.md).
The 0.67 tests exercise the shared CE/DE guards with small coroutines, the texture expansion
policy with software upload/copy callbacks, and the timer backend with injected failures.
The Windows build also runs real waitable timers, nested APC sleeps and thread-exit handle
checks. These tests do not run the full PM4 processor or validate Vulkan texture rendering.
Current native-test gaps and Windows verification steps are recorded in the
[review notes](docs/sotc/REVIEW-2026-10-25.md).

### Preparation diagnostics

`out/analysis.json` describes preparation, not a successful game run. Its `compatibility`
section records the expected and observed bytes at the existing EU 1.01 layout signatures,
inside executable segments. `signature_matches` is not whole-file verification or proof that
every patch applies: each requested FPS code patch also checks its own original instructions.
Neither `TITLE_ID=CUSA08809`, `APP_VER=01.01`, nor the directory name authorizes a patch.
An `HP` publisher prefix (for example `HP9000-...AS`) is an Asian content hint, not proof that
the executable shares or differs from the EU layout. Do not change metadata to force it.

`resource_roots` records the actual base/update `dvdroot_ps4` folders. Resource counts and bytes
are a preparation-time snapshot of the update overlay, not a complete-dump check. An empty
inventory warns about a missing/wrong base path or an executable-only dump. The launcher's
small `out/app-update` directory intentionally has no game assets; it is no longer used as
the resource root. Resource changes alone do not invalidate the executable preparation cache.

Re-run `python scripts/prepare.py "path/to/game" --out out` to obtain these diagnostics.
When inspecting an update/merged executable separately, add `--app0 "path/to/base-game"`
and, if applicable, `--update "path/to/update"`. This command only prepares the image; it
does not link modules, launch the game or test Vulkan.

`out/bb-probe.exe --vulkan-only` (or `bin/bb-probe.exe` in a Windows package) independently
tests a Vulkan command submission and 4096-byte readback. This is not a shader, presentation
or gameplay test. Preparation leaves `validation.execution` and `validation.vulkan` as
`not_run`; keep the Vulkan output and a real `user/last_run.log` as separate evidence.

## Credits

- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28 and
  forks): loader, native runtime and launcher. Original README: [docs/bbport](docs/bbport/README.md).
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and the shadPS4 SotC fork: Vulkan video core.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): culling cvars, BPE
  heap guard, periodic submission, command recording thread, shader fixes (fetch-shader
  constants, `V_ALIGNBIT`, `V_CVT_PK_U8_F32`), dynamic T# tables (`dynamic_tsharp_array_size`),
  the LOD statistics approach and direct memory access for SotC.
- Community pull requests and forks: **Oussama268**, **Lucas-blip-png**, **Gabriell-os**,
  **jownatan**, **xeodeo**, **nakaionyt / y7ngboy**, **tabnul**, **EdouardSence**, **Einar**,
  **KingJhoss**, **nosekefik**, **raulbezerro-dev** and everyone who reports issues with logs.
- 60 FPS patch: illusion (GoldHEN cheats).

License: **GPL-2.0-or-later** ([LICENSE](LICENSE)).
