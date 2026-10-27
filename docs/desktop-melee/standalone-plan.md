# Making Desktop Melee standalone: what to remove from melee-pc

Status: **proposal, nothing removed yet.** Written 2026-10-27 against commit
`009e848` (v0.1.0 release prep). Approve or change it before anything is
deleted.

Desktop Melee is a fork of [melee-pc](https://github.com/999sian/melee-pc). It
still carries melee-pc's other ports, its netplay, its launcher, its updater
and its website, most of which the macOS overlay never uses. This plan says
what can go, what has to stay, what has to be pulled out of a file before that
file can go, and in which order.

## In short

| Piece | In the macOS app today? | Plan |
|---|---|---|
| `platforms/` android, ios, windows, linux, browser | No | Delete |
| `platforms/macos` | Only as a packaging input (`melee.icns` is copied but never used) | Delete after forking the packaging script |
| `docs/site/`, `ROADMAP.md`, `Containerfile`, `docs/netcode-plan.md`, `docs/netplay-verification.md` | No | Delete |
| `MELEE-PC.md`, `docs/building.md`, `docs/testing.md` | Docs only, linked from ours | Move what we need, then delete |
| Netplay (`src/pc/net*.c`, `extern/dht`, `extern/monocypher`, `src/pc/mdns`, rank maths) | Linked in, never run | Replace with a no-op stub, delete |
| Rollback snapshot sections (Mach-O) | Built in, used only by netplay | Delete together with netplay |
| Launcher (`src/pc/launcher.cpp`, RmlUi) | Runs hidden at startup: opens the disc, waits for shaders, holds the settings | Extract about 150 lines, delete the rest, turn RmlUi off |
| F1 menu (in `launcher.cpp`) | Probably reachable (see 2.4) | Delete, after deciding where its settings go |
| Updater (`src/pc/updater.cpp`, libcurl) | Linked, switched off for Desktop Melee | Delete |
| Disc check (`src/pc/launcher_data.cpp`) | Yes: `verify_disc`, `inspect_disc`, `launcher.cfg` | Keep as is |
| `tools/` (173 files) | 4 to 7 files used by the build | Keep those, delete the rest |

A build without netplay, the snapshot sections, the launcher, the updater,
RmlUi and libcurl was configured, built and linked (section 1.3). Its stripped
binary is **23.9 MB instead of 26.8 MB**. Deleting the files outside the build
(platforms, docs, tools) was checked by configuring only (section 1.3,
experiments 10 and 11). Together the repository loses roughly 25,000 lines of
melee-pc code plus 60 platform files and about 160 tools.

**One thing a link can't show:** the netplay stub must return what the real
code returns when no session is running, not just zeros. One stubbed
function, `pc_net_audio_record`, sits on the path of every sound effect and
must return its argument. A plain zero stub would hand the game voice 0 for
every sound (section 2.3).

## 1. How this was checked

### 1.1 Labels

Each claim below carries one of these:

- **[build]**: shown by an actual configure, build or link (section 1.3).
- **[code]**: read in the code or found with `git grep`; the file and line are
  given.
- **[runtime]**: needs the app to run (`desktop-melee/tests/run_all.sh`, about
  9 minutes, takes over the screen, or a manual check). **None of the runtime
  checks were done for this plan.**

### 1.2 The build today

`desktop-melee/package.sh` → `tools/package_macos.sh` (CMake configure, then
`ninja melee`, then stage `Melee.app`) → back in `package.sh`: rename, Desktop
Melee's `Info.plist`, app icon via `actool`, font, sign, install.
`desktop-melee/build.sh` is the development path with the same flags.

What `ninja melee` builds [code: `CMakeLists.txt`, `desktop-melee/desktop-melee.cmake`, `cmake/AppleSnapshot.cmake`]:

- `melee_game` (static library): the 994 decompiled game files in `src/melee`
  and `src/sysdolphin` (pinned by `cmake/game_sources.manifest`),
  `src/pc/widescreen.c`, `desktop-melee/game/*.c`. Compiled by Homebrew GCC via
  `tools/gcc_launcher.py`, wrapped by `tools/macho_snapshot_compile.py`
  (which imports `tools/macho_snapshot.py` and `tools/pe_snapshot_compile.py`).
- `melee` (the executable): `PC_SOURCES` in `CMakeLists.txt` (all of
  `src/pc/*.c|cpp` that is listed, netplay included), `extern/dht`,
  `extern/monocypher`, `src/pc/melee_state_macho.c`, `desktop-melee/src/*`,
  and the Swift library `desktop_melee_ui`. Links libcurl (macOS's own) for
  the updater.
- aurora (`extern/aurora`, vendored, with Desktop Melee's changes): core, gx,
  gd, vi, pad, si, mtx, os, dvd, card, thp, main, plus RmlUi because the root
  `CMakeLists.txt` forces `AURORA_ENABLE_RMLUI ON`. Fetched at configure time:
  Dawn and nod (prebuilt), SDL3, RmlUi, imgui, tracy, googletest.
- `launcher_resources` copies all of `resources/` next to the binary;
  `pipeline_cache` unpacks `tools/initial_pipeline_cache.db.gz`.
- After linking, `tools/macho_snapshot_verify.py` checks the rollback
  sections.

What packaging reads [code: `tools/package_macos.sh`]: `platforms/macos/Info.plist`
(then replaced by `desktop-melee/macos/Info.plist`), `platforms/macos/melee.icns`
(copied into the app but unused: our `Info.plist` names `AppIcon`),
`resources/`, `tools/initial_pipeline_cache.db.gz`, the root `VERSION`.

What runs at startup in overlay mode [code: `src/pc/main.c:584-716`]:
`pc_gcadapter_init` → `pc_launcher_configure` (reads `launcher.cfg`) →
`desktop_melee_configure` → `aurora_initialize` → controller mappings from
`resources/gamecontrollerdb.txt` → `desktop_melee_choose_disc` (the welcome
window) → `pc_launcher_run` (the launcher's RmlUi document, never shown) →
`pc_menu_init` (applies saved settings, loads the F1 menu) →
`desktop_melee_after_menu_init` → `melee_main`.

### 1.3 Experiments

All in a scratch build directory with `build.sh`'s exact flags; the
repository was restored after each one and is unchanged. The lists that
matter for the work are in the appendix.

| # | Change | Result |
|---|---|---|
| 0 | Baseline | Builds, links, rollback check passes. 29.6 MB, 26.8 MB stripped. |
| 1 | Drop `updater.cpp` | 6 undefined symbols, all from `launcher.cpp`. |
| 2 | Drop netplay (24 lines of `PC_SOURCES`) | 95 undefined symbols, from 21 files (section 2.3). |
| 3 | 2 + a generated no-op stub | Links. The post-link rollback check then fails: "missing symbol net". |
| 4 | 3 + Apple snapshot sections off (full rebuild of the game code) | Builds and links. No `__melee_data` section. |
| 5 | Drop `launcher.cpp` and `updater.cpp` | 25 undefined symbols (section 2.4). |
| 6 | 5 + a 150-line replacement without RmlUi | Links; rollback check passes. |
| 7 | 4 + 6 + `AURORA_ENABLE_RMLUI OFF` + no libcurl (full rebuild) | Builds and links. 0 RmlUi symbols (5,503 before), no `libcurl.4.dylib`, 0 updater symbols. 26.3 MB, **23.9 MB stripped**. |
| 8 | Build `desktop_melee_disc_check` | Builds from `launcher_data.cpp` + `disc_open.c` alone. |
| 9 | Drop `android_compat.cpp`, `windows_arm64_softfp.c`, `touch.c` | 7 undefined symbols (section 2.6). |
| 10 | Copy of the tree without the files of section 6, step 2 (other platforms, `docs/site`, `ROADMAP.md`, `Containerfile`, `mods/`, `tests/browser/`, non-Apple `cmake/` files, other platforms' tools), configure only | Fails at `CMakeLists.txt:391-392` (`include(cmake/WindowsSnapshot.cmake)` and its call). With those two lines removed, configures. |
| 11 | 10 + delete all of `tools/` except the files section 2.7 keeps, configure only | Fails: `version_test`, `endian_test`, `thp_test` and `updater_test` (`CMakeLists.txt:476-492` and `642-652`; `launcher_data_test` was kept with its source) need their `tools/test_*.cpp` sources even though they are `EXCLUDE_FROM_ALL`. |
| 12 | 3 with `net` removed from `--excluded` in `cmake/AppleSnapshot.cmake` | Builds, links, rollback check passes (section 6, step 4 as written). |

Not done: packaging an app from these builds, running it, or running
`run_all.sh`. A successful link proves nothing calls the removed code from
the binary; it doesn't prove the app behaves the same.

## 2. Candidate by candidate

### 2.1 `platforms/`

- **android, ios, windows, linux, browser: unused.** [code] They are only
  reached through the `EMSCRIPTEN`, `IOS`, `ANDROID` and `WIN32` branches of
  `CMakeLists.txt` and through `tools/build_android.sh`, `build_ios.sh`,
  `package_windows.sh` and `package_linux.sh`. Deleting them breaks nothing on
  macOS. The dead CMake branches can go in the same step or later.
- **macos: a packaging input only.** [code] `tools/package_macos.sh` copies
  `Info.plist` (which `package.sh` overwrites) and `melee.icns` (which nothing
  uses). Fork the packaging script into `desktop-melee/` first, then delete.
- **History.** The big Android files are melee-pc commits:
  `platforms/android/app/libs/arm64-v8a/libmelee.so` (34.5 MB) and
  `.../jniLibs/x86_64/libSDL3.so` (10.8 MB), added in `8eb677c` and untracked
  in `5fd6255`, both by 999sian. Deleting the tree doesn't shrink a clone; the
  whole pack is 46 MB today. Rewriting history to drop them would give every
  commit a new hash, cut the shared history with melee-pc (no more `git merge`
  from it, ever) and break every existing clone and link to a commit.
  **Recommendation: don't rewrite.** If clone size ever matters, a fresh
  repository without history is the honest version of the same thing.
- Note: `CONTRIBUTING.md` says Windows and Linux are planned. After this plan,
  melee-pc is the reference for those ports, and they would come back as
  Desktop Melee code, not as melee-pc's trees.

### 2.2 Documents and repository files

| File | Used? | Plan |
|---|---|---|
| `docs/site/` | melee-pc's website (one `index.html`); its Pages workflow is already removed | Delete; drop `!docs/site/` from `.gitignore` |
| `ROADMAP.md` | melee-pc's roadmap; linked only from `MELEE-PC.md` | Delete |
| `docs/netcode-plan.md`, `docs/netplay-verification.md` | Netplay design; mentioned in comments in `CMakeLists.txt`, `extern/aurora/lib/dolphin/dvd/dvd.cpp`, `src/melee/gm/forward.h`, `gmonlinemode.c` | Delete with netplay; the comments can stay (they name melee-pc's file) |
| `Containerfile` | Linux build image for `package_linux.sh` | Delete |
| `MELEE-PC.md` | Linked from `README.md` (twice), `docs/debugging.md` and `how-it-works.md` (for the `MELEE_*` variables) | Move the variables Desktop Melee uses into `how-it-works.md` section 18, then delete, and link melee-pc's repository instead |
| `docs/building.md`, `docs/testing.md` | melee-pc's build (Linux, Windows, Android, container) and ctest suite | Delete once nothing links them |
| `docs/architecture.md`, `docs/debugging.md`, `docs/porting-notes.md` | Still describe code we keep; `how-it-works.md` links `porting-notes.md` | Keep for now, trim later |
| `.github/workflows/build.yml` | melee-pc's multi-platform check, manual only | Replace with a macOS job that runs `desktop-melee/build.sh`, or delete |
| `.github/ISSUE_TEMPLATE/`, `CODING_STYLE.md`, `CONTRIBUTING.md` | Ours | Keep |
| `CMakePresets.json` | Presets for melee-pc's platforms | Trim to macOS or delete |
| `mods/` | Only a `.gitkeep`; nothing in the code reads it | Delete |

### 2.3 Netplay

**Not used, but linked in and woven into the game code.** Desktop Melee boots
into `GM_DESKTOP_MELEE` and never reaches `GM_ONLINE` [code:
`src/melee/gm/gmboot.c:59-62`]. But removing the netplay sources leaves 95
undefined symbols [build, experiment 2], referenced from:

- game code: `gmonlinemode.c` (51), `axdriver.c` (14), `mnstagesel.c` (11),
  `ifnet.c` (7), `gmscene.c` (4), `ifnetchat.c` (3), `lbfile.c`,
  `lbaudio_ax.c` (2 each), `rumble.c`, `ifmagnify.c`, `grpstadium.c`,
  `ftwobble.c`, `ftnanaspecials.c`, `ftdrawcommon.c`, `efasync.c`, `devcom.c`
  (1 each);
- port code: `vi.c` (15), `launcher.cpp` (4), `slp.c` (3), `main.c` (3),
  `os.c` (2).

**Plan: one stub file instead of editing those call sites.** A file of 95
small functions replaces all netplay code [build, experiments 3, 4 and 7].
The game files keep calling `pc_net_*`, so melee-pc's game-code fixes keep
merging cleanly. The game includes 9 of the netplay headers, so the headers
stay. The stub in the experiments was generated from those headers and
returns zero, false or NULL everywhere. **That is good enough to link, not to
ship.** The real stub has to return what the real function returns when
there is no session (`net.active` false, `MELEE_NET` unset). From the real
code [code, `src/pc/net*.c`]:

| Function | Offline value | Why it matters |
|---|---|---|
| `pc_net_audio_record(v)` | `v` | **Every sound effect:** `HSD_AudioSFXStartParam` returns it as the voice id (`axdriver.c:650`). Zero would give every sound voice 0. |
| `pc_net_recv_reliable` | `-1` | `mnstagesel.c:190` loops `while (... >= 0)`: zero would spin forever. Guarded today by `pc_net_active()`. |
| `pc_rank_session_stage_port` | `-1` | Read as a port number in `mnstagesel.c`. |
| `pc_net_sync` and `pc_net_sim_frame` | `pc_net_sync` counts frames even offline (`net.c:3274-3276`, called every tick from `gmscene.c:433`); `pc_net_sim_frame` returns that count minus 1 | The frame numbers in Slippi recordings (`slp.c:345`). A constant would stamp every frame -1. The stub needs the counter. |
| `pc_net_frame` | `-1` (`net.tick_frame` is only set online) | Log lines in `vi.c`. |
| `net_sfx_resolve`, `net_sfx_keyoff`, `net_sfx_set` | `-1` | Only reached for netplay handles, which never exist offline. |
| `pc_net_match_local_code`, `pc_net_match_profile_directory`, `pc_net_chat_prompt`, `pc_lan_local_name`, `pc_net_match_opponent_code`, `pc_net_chat_line` | `""`, not NULL | Strings; a NULL would crash a caller that prints them. |
| `pc_rank_session_seconds`, `pc_rank_session_stocks` | `480`, `4` | Ranked-set rules. |
| Everything else | `0`, `false`, or nothing | Already what the real code returns offline. |

Of these, `pc_net_audio_record` is on a path Desktop Melee runs all the time,
and `pc_net_sync`/`pc_net_sim_frame` when `MELEE_SLP_DIR` is set; the others
sit behind `pc_net_active()` or online-only screens [code]. The stub should
still match all of them, so a melee-pc game change that drops one of those
guards can't turn into a hang or a crash here.

Deleted: `src/pc/net*.c` (12,123 lines; the headers stay), `extern/dht/` (MIT), `extern/monocypher/` (BSD-2-Clause or CC0),
`src/pc/mdns/` (only `net_lan.c` includes it), `src/pc/libm/pc_rank_*.c`,
`resources/dht-nodes-v1`, and the netplay tests and harnesses in `tools/`
(`net_*.py`, `test_net_*`).

**The rollback snapshot sections go with it.** They exist so netplay can save
and restore the game state; only `net.c` and `net_snapshot.c` read them
[code]. With netplay gone, the post-link check fails because it looks for
netplay's `net` global [build, experiment 3]. Removing them means deleting the
`melee_enable_apple_snapshots` call, the "sections are required" error and the
`MELEE_STATE_SECTIONS` define in `CMakeLists.txt`, plus
`cmake/AppleSnapshot.cmake`, `src/pc/melee_state_macho.c`, `melee_state.h`,
`melee_state.ld` and the four snapshot scripts in `tools/`. Every game file is
then compiled without the section wrapper and without `-fno-common`, which a
full rebuild showed works [build, experiment 4]. Game globals move from the
`__melee_data` section to the default ones; nothing outside netplay looks at
them [code], but this is the change most worth a full `run_all.sh`
[runtime].

**Kept on purpose:** the floating-point determinism settings (`-ffp-contract=off`,
the musl trig routing in `src/pc/libm/`, `MELEE_FP_PERTURB`). They were added
for netplay, but they also decide the game's exact numbers. Removing them
would make Desktop Melee's physics differ from melee-pc's by rounding and gains
nothing. `MELEE_FP_PERTURB` and its comments can go later.

**Later, optional:** melee-pc's online screens inside the game code
(`gmonlinemode.c`, `ifnet.c`, `ifnetchat.c`, `mnonline.c`, `mnonlinelobby.c`,
and `GM_ONLINE` in the scene tables). The stub covers them, and removing them
means editing the game's scene tables, which conflicts with every melee-pc
change there.

What changes for players: nothing, if the stub follows the table above.
Nothing in Desktop Melee reaches online play. `pc_is_ucf_enabled` and
`pc_is_free_camera_enabled` ask `pc_net_deterministic()`, which the stub
answers "no", the same as offline today [code]. Not yet shown by a run
[runtime].

What changes for developers: melee-pc's determinism tools live in the
netplay code and go with it: `MELEE_NET_RECORD` / `MELEE_NET_REPLAY` (record
inputs and replay them with state checksums), `MELEE_NET_SYNCTEST`,
`MELEE_NET_STATE_LOG`, and the other `MELEE_NET_*` variables. Desktop Melee's
tests use none of them. `MELEE_SEED`, `MELEE_KEY_FIFO`, `MELEE_BOOT_SCENE`
and `MELEE_DEBUG_VS*` are in the game code and `keyboard.c`, and stay.

### 2.4 The launcher and the F1 menu (`src/pc/launcher.cpp`, RmlUi)

**Used, but only a small part.** In overlay mode the launcher's RmlUi document
is created and never shown [code: `launcher.cpp:942-945`], and its update check
is skipped [code: `launcher.cpp:949-952`]. What Desktop Melee actually gets
from the file:

1. **Settings.** It loads and saves `launcher.cfg`, and it is where the game
   and our menu bar read and write them: `pc_prefs_get/set_audio`,
   `pc_prefs_get/set_ucf`, `pc_prefs_save`, `pc_prefs_get_disc` (ours), and
   melee-pc's `pc_is_*_enabled`, `pc_get_hud_mode`, `pc_get_key_binding`,
   `pc_install_id`, `pc_app_rev`, `pc_get_net_*`, `pc_set_net_target`.
2. **Startup settings** in `pc_launcher_configure` [code: `launcher.cpp:1305-1392`]:
   backend Metal, MSAA, anisotropy, vsync, fullscreen. It also scans
   `~/Documents` for a disc image when `launcher.cfg` names none, and that
   path becomes the welcome window's suggestion through `pc_prefs_get_disc`
   [code: `desktop_melee.c:714-724`].
3. **Opening the disc**: `inspect_disc`, `aurora_dvd_open`,
   `pc_load_disc_fonts`, then save the path [code: `launcher.cpp:1410-1425`
   and `begin_game`, `launcher.cpp:666-685`].
4. **The shader wait**: a loop of `aurora_update`, an empty frame and
   `aurora_wait_pipelines(8)` until `aurora_wait_pipelines(0)` is 0, while our
   loading window shows the count [code: `launcher.cpp:1263-1299`; ours:
   `desktop_melee_pipelines_pending`]. Desktop Melee forbids skipping it
   [code: `launcher.cpp:737-740`].
5. **Applying settings before the game starts** in `pc_menu_init`: widescreen
   mode, render scale, volumes, reverb, texture filter [code:
   `launcher.cpp:2275-2282`].

**The disc verification is not in `launcher.cpp`.** `verify_disc`,
`inspect_disc`, the SHA-1 and the `launcher.cfg` reader and writer all live in
`src/pc/launcher_data.cpp`, which has no RmlUi, and which our
`desktop-melee/src/disc_check.cpp` already calls directly. The disc check tool
builds from that file and `disc_open.c` alone [build, experiment 8]. **Keep
`launcher_data.cpp` and `launcher_data.hpp` unchanged.**

Without `launcher.cpp` and `updater.cpp`, 25 symbols are missing [build,
experiment 5]: the five `pc_launcher_*`/`pc_menu_*` entry points, the settings
getters listed in point 1, and the six `pc_prefs_*` functions our Swift and C
code call. A 150-line replacement that implements them over
`launcher_data.cpp`, with the five points above and no RmlUi, links and passes
the rollback check [build, experiment 6], and builds with RmlUi switched off
[build, experiment 7]. Appendix A lists what it has to provide.

**Extract into:**

- `src/pc/prefs.cpp`: the `launcher.cfg` state, `pc_launcher_configure`'s
  startup settings, the getters and setters, and `pc_menu_init`'s
  apply-settings part (renamed, for example `pc_prefs_apply`). It is melee-pc
  code moved, so it keeps melee-pc's notice (section 5).
- `desktop-melee/src/startup.cpp`: open the disc, wait for the shaders. This
  replaces `pc_launcher_run` in `main.c`, and the
  `desktop_melee_launcher_wait_begin/end` hooks can fold into it.

**Decisions to make before deleting:**

- **The F1 menu.** melee-pc's in-game settings menu is part of
  `launcher.cpp`. `vi.c` opens it on F1 [code: `vi.c:220-222`], and Desktop
  Melee makes the overlay the key window during a match so keyboard play
  works (`how-it-works.md` section 15), so F1 probably opens it on top of the
  desktop today [runtime: not checked]. Removing it removes, from the UI:
  keyboard key bindings, render scale, texture filter, FPS counter, custom
  textures, unlock-all, frozen Pokémon Stadium, free camera and melee-pc's HUD
  mode. They stay in `launcher.cfg` and keep working. Volumes and UCF are
  already in our menu bar. **Recommendation:** delete the F1 menu, and add
  the keyboard bindings to the Controls tab if keyboard players need to
  rebind. The welcome window already shows the bindings through
  `pc_get_key_binding`.
- **The `~/Documents` disc scan.** Keep it in `prefs.cpp` (first run suggests
  a disc it found) or drop it (the welcome window always asks). The prototype
  dropped it. Recommendation: keep, it is 40 lines and saves a step.

**Removed with it:** `resources/launcher.rml`, `launcher.rcss`,
`port-menu.rml`, `port-menu.rcss`, `fps.rml`, `resources/launcher/` (art), the
five fonts and `FONT-LICENSE.txt` (Liberation Sans and Noto Sans, used only by
RmlUi [code: `launcher.cpp:136-150`]), `tools/launcher_art/`, and the
`AURORA_ENABLE_RMLUI ON` line. Only `resources/gamecontrollerdb.txt` stays
[code: `main.c:644-649`].

**Careful with `resourcesPath`.** `main.c:537-560` points aurora's
`resourcesPath` at `<app>/Contents/Resources/resources/` for RmlUi, but aurora
also looks for the shader seed `initial_pipeline_cache.db` there, then falls
back to the directory beside the executable, which for the app is
`Contents/Resources/`, where packaging puts it [code:
`extern/aurora/lib/gfx/pipeline_cache.cpp:778-807`]. Dropping the setting is
fine (aurora then defaults to `SDL_GetBasePath()`, the same directory), but
the seed has to stay in `Contents/Resources/`, or every pipeline compiles on
first use. Check the loading window's pipeline count after the change
[runtime].

**RmlUi off and the shader cache.** With RmlUi off, aurora stops loading and
pruning the cache's RmlUi rows (`ShaderType::Rml`); the file format, the GX
rows and players' existing caches in the `melee-pc` folder are unaffected
[code: `pipeline_cache.cpp:1171-1180`, `1442-1444`]. The seed holds 20 RmlUi
rows next to 11,881 GX rows and 4 clear rows, so the startup count drops by
about 20.

**Must not change:** where settings live. `launcher.cfg`, the memory card and
the shader cache are in `~/Library/Application Support/melee-pc/`
(`SDL_GetPrefPath(NULL, "melee-pc")`, `src/pc/paths.c:27`), and the tests read
`launcher.cfg` there. Renaming that folder loses players' saves and unlocks.
If it is ever renamed, copy the old folder once on first launch, as
`Settings.swift` did for the `melee-pet` defaults suite.

### 2.5 The updater (`src/pc/updater.cpp`)

**Not used.** Only `launcher.cpp` calls it (6 symbols) [build, experiment 1],
and Desktop Melee switches the update check off [code: `launcher.cpp:949-952`].
It is the only user of libcurl [code: `git grep MELEE_USE_CURL`], so the
`find_package(CURL)` block for Apple goes too; the minimal build has no
`libcurl.4.dylib` [build, experiment 7]. `tools/test_updater.cpp` and the
`updater_test` target go with it.

`src/pc/version.cpp` stays (the `--version` output and `pc_app_rev`). It
reports the root `VERSION`, which is melee-pc's release; after the split it
should report `desktop-melee/VERSION`, and the root `VERSION` can then go
(`package.sh` and `package_macos.sh` both read version files).

### 2.6 Other files in `src/pc/`

| File | On macOS | Plan |
|---|---|---|
| `android_compat.cpp` | Builds no-op fallbacks for 5 Android hooks [build, experiment 9] | Delete once netplay and the launcher are gone; then only `main.c`'s `pc_android_set_launcher_active` calls remain, which go too |
| `android_hooks.h`, `ios_dialog.h/.m`, `melee_state_pe.c`, `windows_arm64_softfp.c` | Not built, or build to nothing | Delete (with `main.c`'s and `launcher.cpp`'s includes of `android_hooks.h`) |
| `touch.c` | Builds on macOS (`__APPLE__` guard); `vi.c` and `keyboard.c` call it | Keep for now; stubbing it is a later cleanup |
| `slp.c`, `slp_format.c` | Slippi replay recording (`MELEE_SLP_DIR`); the game calls it | Keep (it calls 3 netplay functions, which the stub answers) |
| `gcadapter.c` | Our code calls `pc_gcadapter_raw` | Keep |
| `keyboard.c` | `MELEE_KEY_FIFO`, which our tests drive matches with | Keep |
| `textures.cpp`, `file_cache.cpp`, `music_stream.cpp`, `thp*`, `audio.c`, `gx.c`, `vi.c`, `os.c`, `paths.c`, `region.c`, `discfont.c`, `disc_open.c`, `input_poll.c`, `misc.c`, `widescreen.c`, `vtxarray.c`, `stb_*`, `libm/` (trig) | The port itself | Keep |

### 2.7 `tools/`, `tests/`, `cmake/`

- **`tools/` keeps** `gcc_launcher.py`, `initial_pipeline_cache.db.gz`, and
  `gen_pipeline_cache.py` (to regenerate that seed; its merge mode stands
  alone, its `--run` mode needs `devctl.py` and X11, so it goes or gets
  rewritten). `package_macos.sh` stays
  until it is forked into `desktop-melee/`. The four snapshot scripts
  (`macho_snapshot_compile.py`, `macho_snapshot.py`, `macho_snapshot_verify.py`,
  `pe_snapshot_compile.py`; the Apple wrapper imports the last one) stay until
  the snapshot sections go. `check_style.py` can stay if Desktop Melee wants a
  style check (it runs `clang-format`).
- **`tools/` deletes** everything else (about 160 files): the other platforms'
  build and launcher scripts (`gcc_ios_launcher.py`,
  `gcc_windows_arm64_launcher.py`, `build_android.sh`, `run_proton.sh`,
  `browser/`, ...), the netplay harnesses, the Linux-only ctest suite
  (`test_*.py`, `test_*.c`) and melee-pc's development scripts.
  Desktop Melee's own tests and scripts use none of them [code]. melee-pc's
  four unit tests that build on macOS (`test_launcher_data.cpp`,
  `test_version.cpp`, `test_endian.cpp`, `test_thp.cpp`) can be kept if we
  want a `ctest` for the code we keep; `test_launcher_data.cpp` covers the
  disc check, so it is the one worth keeping. **The test targets must go in
  the same commit as their sources:** CMake refuses to configure when a
  target's source file is missing, even for `EXCLUDE_FROM_ALL` targets
  [build, experiment 11]. Those are `CMakeLists.txt:468-492` (launcher_data,
  version, endian, thp), `496-640` (Linux only, not evaluated on macOS, but
  dead) and `642-652` (updater).
- **`tests/browser/`**: the browser port's tests. Delete.
- **`cmake/` keeps** `GameSources.cmake`, `game_sources.manifest`,
  `MeleeToolchainCheck.cmake`; **deletes** `WindowsSnapshot.cmake`,
  `ios.toolchain.cmake`, the two `*-mingw32.cmake` toolchains, and later
  `AppleSnapshot.cmake`.
- **`CMakeLists.txt`**: after the steps below it is ours. The Windows, Linux,
  Android, iOS and Emscripten branches and the Linux-only test registrations
  (lines 496-640 today) go; that removes about half the file.

## 3. Keep, delete, extract

**Keep**

- `src/melee/`, `src/sysdolphin/`, `src/sdk_include/`, `src/Runtime`, `src/thp`
  and the rest of the game tree, untouched apart from our tagged hooks.
- `extern/aurora/` with our changes.
- `src/pc/` as listed in section 2.6, the netplay headers, `launcher_data.cpp`,
  and the new `prefs.cpp` and stub.
- `resources/gamecontrollerdb.txt`; `cmake/GameSources.cmake`,
  `game_sources.manifest`, `MeleeToolchainCheck.cmake`;
  `tools/gcc_launcher.py`, `initial_pipeline_cache.db.gz`,
  `gen_pipeline_cache.py`; `licenses/`.

**Delete**

- `platforms/` (all of it, after forking the packaging script).
- `docs/site/`, `ROADMAP.md`, `Containerfile`, `docs/netcode-plan.md`,
  `docs/netplay-verification.md`, then `MELEE-PC.md`, `docs/building.md`,
  `docs/testing.md`; `mods/`; `tests/browser/`.
- `src/pc/net*.c`, `extern/dht/`, `extern/monocypher/`, `src/pc/mdns/`,
  `src/pc/libm/pc_rank_*.c`, `resources/dht-nodes-v1`.
- The Mach-O snapshot sections and their scripts.
- `src/pc/launcher.cpp`, `src/pc/updater.cpp` and the RmlUi resources;
  `android_compat.cpp` and the other platform-only `src/pc` files.
- About 160 files in `tools/`; the non-Apple CMake toolchains and branches.

**Extract**

- From `launcher.cpp`: settings and startup settings → `src/pc/prefs.cpp`;
  disc opening and the shader wait → `desktop-melee/src/startup.cpp`.
- From `tools/package_macos.sh`: the build, staging, dylib bundling and
  checks → `desktop-melee/package.sh` (or a script beside it), without
  `platforms/macos`, the `melee.icns` copy and melee-pc's zip step.
- From `MELEE-PC.md`: the `MELEE_*` variables Desktop Melee documents →
  `how-it-works.md` section 18.

## 4. Taking melee-pc fixes after the split

Today `upstream` is melee-pc and the merge base with `upstream/main` is
`7f0bc6b`. Once these directories are deleted, a plain `git merge upstream/main`
goes wrong in two ways: every melee-pc change to a deleted file is a
modify/delete conflict, and every **new** melee-pc file under a deleted
directory (a new `net_*.c`, a new Android file) is added back without any
conflict at all. `CMakeLists.txt` and `main.c` would conflict on almost every
merge.

**Recommendation: stop merging, start picking.** This matches "consult
melee-pc only when needed".

1. Right before the split, merge `upstream/main` one last time, so the split
   starts from melee-pc's newest fixes.
2. Record that commit in `desktop-melee/UPSTREAM_MELEE_PC` (the hash and the
   date). `src/UPSTREAM_COMMIT` is the decompilation's commit, not
   melee-pc's; leave it alone.
3. To catch up later, look only at the paths we still share:

   ```sh
   git fetch upstream
   git log --oneline "$(cat desktop-melee/UPSTREAM_MELEE_PC)"..upstream/main -- \
       src/melee src/sysdolphin src/sdk_include extern/aurora \
       src/pc/audio.c src/pc/gx.c src/pc/vi.c src/pc/os.c src/pc/main.c \
       src/pc/launcher_data.cpp src/pc/keyboard.c src/pc/input_poll.c ...
   ```

   Then `git cherry-pick -x` the fixes worth having, resolve, and move the
   recorded commit forward. Fixes to `launcher.cpp` settings code have to be
   carried over to `prefs.cpp` by hand.
4. Game-code fixes are what matter most, and they should keep applying
   cleanly, because this plan doesn't touch game files: netplay is stubbed,
   not cut out of the call sites.

If whole merges are still wanted, keep a list of deleted paths and a script
that runs the merge with `--no-commit`, then runs `git rm -r` on every listed
path (which also drops files melee-pc added there) and shows what is left to
resolve. That works, but each merge still costs a `CMakeLists.txt` and
`main.c` conflict. Not recommended.

Shared history stays either way (section 2.1), so `git log upstream/main`,
`git blame` and cherry-picks keep working.

## 5. Licensing

The license situation doesn't change in kind: the port code stays
GPL-3.0-or-later, the game code stays unlicensed, the third-party parts keep
their own licenses. What changes is bookkeeping.

- **Moved code keeps its authors.** `prefs.cpp`, `startup.cpp`, the netplay
  stub and the forked packaging script are made from melee-pc files. They keep
  the `SPDX-License-Identifier: GPL-3.0-or-later` line, say which melee-pc
  file they come from, and carry "Copyright (C) 2026 the melee-pc
  contributors" next to Desktop Melee's own line. They can't be relabeled as
  Desktop Melee's alone. GPL section 5a (prominent notice of changes) is met
  by that header and by the list in `licenses/LICENSE.md`.
- **`licenses/LICENSE.md` section 2** ("`src/pc/`, `tools/`, `platforms/`,
  `cmake/` and `.github/` are original work for this port"): remove
  `platforms/`, and say that `CMakeLists.txt`, `src/pc/main.c` and the
  extracted files are melee-pc files changed by Desktop Melee.
- **The list of changed melee-pc files** (the "Desktop Melee" subsection)
  grows: `CMakeLists.txt` (heavily), `src/pc/main.c`, `src/pc/vi.c`,
  `src/pc/keyboard.c` (F1 menu calls), `tools/package_macos.sh` (if changed in
  place instead of forked), and the new derived files. `src/pc/launcher.cpp`
  comes off the list, since it is deleted.
- **Section 3 (third-party)**: remove `extern/dht` (MIT), its `sha1.c` (public
  domain), `extern/monocypher` (BSD-2-Clause or CC0) and the Android SDL Java
  sources (zlib); remove the Liberation Sans entry and `FONT-LICENSE.txt` with
  the fonts. RmlUi drops off the "fetched at build time" list.
- **Missing today, worth fixing in the same pass:** section 3 doesn't list
  `src/pc/libm/` (musl, MIT; its `LICENSE` file is there), `src/pc/stb_*`
  (public domain or MIT), `src/pc/mdns/` (public domain; deleted with
  netplay anyway), or the Noto Sans fonts (SIL OFL, named in
  `FONT-LICENSE.txt` but not in `LICENSE.md`).
- **`README.md`**: its license summary lists "MIT, zlib, BSD, OFL". After the
  split that is MIT (aurora, musl), public domain (stb) and the fetched
  dependencies' own licenses (Dawn, SDL3, nod, imgui, tracy).
- **Shipped app:** the `.app` today contains the RmlUi fonts and their license
  file. After the split it doesn't; check that the app still ships the GPL
  text and the notices for what it does contain.

## 6. Step order

One commit (or a small PR) per step. After every step that changes the
binary: `desktop-melee/build.sh`, `ninja -C build/macos desktop_melee_disc_check`,
`desktop-melee/package.sh`, then `desktop-melee/tests/run_all.sh` (9 minutes,
takes over the screen) and a manual pass: first launch with no
`launcher.cfg` (welcome window, disc pick, verification), the loading window's
shader count, a match, volume and UCF from the menu bar surviving a restart.

1. **Last full merge from melee-pc** and record it in
   `desktop-melee/UPSTREAM_MELEE_PC` (section 4). Optional, but it is the last
   cheap moment.
2. **Files outside the build** (no binary change): `docs/site/`, `ROADMAP.md`,
   `Containerfile`, `mods/`, `tests/browser/`; `platforms/android`, `ios`,
   `windows`, `linux`, `browser`; the non-Apple `cmake/` files; the other
   platforms' scripts in `tools/`. In the same commit, remove
   `include(cmake/WindowsSnapshot.cmake)` and
   `melee_enable_windows_snapshots(melee melee_game)` (`CMakeLists.txt:391-392`),
   or configure fails [build, experiment 10]. Fix `.gitignore` and the links
   in `README.md`. Check: configure passes (verified on a copy); the `melee`
   sources don't change, so the binary doesn't either.
3. **Packaging**: fork `tools/package_macos.sh` into `desktop-melee/`, drop
   `platforms/macos`. Check: the packaged app matches the old one (same
   bundle contents minus `melee.icns`, same `otool -L`).
4. **Netplay**: add the hand-written stub (offline values from the table in
   section 2.3, including the frame counter), delete the netplay sources,
   `extern/dht`, `extern/monocypher`, `src/pc/mdns`, `pc_rank_*`,
   `dht-nodes-v1`, the netplay tools and docs, and the
   `target_include_directories(melee PRIVATE extern/monocypher)` line. Keep
   the headers. Keep the snapshot sections for now, but drop `net` from the
   post-link check's `--excluded` list in `cmake/AppleSnapshot.cmake`; with
   that, the build links and the check passes [build, experiment 12]. Beyond
   `run_all.sh`, check that match sounds play and stop normally, and that a
   Slippi recording (`MELEE_SLP_DIR`) has increasing frame numbers.
5. **Snapshot sections**: remove them (full rebuild of the game code). The
   biggest behaviour risk in this plan; run `run_all.sh` and play a few
   matches.
6. **Launcher, F1 menu and updater**: after the F1 decision (section 2.4),
   add `src/pc/prefs.cpp` and `desktop-melee/src/startup.cpp`, update
   `main.c`, `vi.c` and `keyboard.c`, delete `launcher.cpp`, `updater.cpp`,
   the RmlUi resources and fonts, turn RmlUi off, drop libcurl, and remove
   the `updater_test` target with `tools/test_updater.cpp`. Check
   `launcher.cfg` compatibility both ways with an existing file, and that the
   loading window still starts from about 11,900 pipelines (the seed was
   found).
7. **Clean-up**: the rest of `tools/` and `cmake/` together with the test
   targets that name them (section 2.7), the dead platform branches in
   `CMakeLists.txt`, `android_compat.cpp` and friends, the
   version files, `.github/workflows/build.yml`, `MELEE-PC.md`,
   `docs/building.md`, `docs/testing.md`, `CMakePresets.json`.
8. **Docs and licenses**: `licenses/LICENSE.md` (section 5), `README.md`,
   `CONTRIBUTING.md`, and `how-it-works.md` (sections 5, 6, 16, 17, 18, the
   touch-points list and the file map).
9. **Later, optional**: melee-pc's online screens in the game code;
   `touch.c`; aurora extras Desktop Melee doesn't need (imgui, tracy).

Steps 2 and 3 can happen any time. Steps 4 and 6 don't depend on each other
(the stub answers `launcher.cpp`'s netplay calls), so either can go first;
step 5 needs step 4.

## Appendix A. What replaces `launcher.cpp` must provide

The symbols left undefined without `launcher.cpp` and `updater.cpp` [build,
experiment 5], and who calls them. `pc_get_net_name`, `pc_get_net_port`,
`pc_install_id` and `pc_app_rev` are only called from netplay files, so once
the stub is in they can go. `pc_set_net_target` is called from game code
(`gmonlinemode.c`), so it stays, in `prefs.cpp` or in the stub.
`pc_is_frozen_stadium_enabled` and `pc_is_ucf_enabled` keep their
`pc_net_rules` / `pc_net_deterministic` checks, answered by the stub.

| Symbol | Called from |
|---|---|
| `pc_app_rev` | net_lan.c net_match.c |
| `pc_get_hud_mode` | ifall.c widescreen.c |
| `pc_get_key_binding` | keyboard.c |
| `pc_get_net_name` | net_match.c |
| `pc_get_net_port` | net_match.c |
| `pc_install_id` | net_lan.c |
| `pc_is_custom_textures_enabled` | textures.cpp |
| `pc_is_free_camera_enabled` | camera.c |
| `pc_is_frozen_stadium_enabled` | grpstadium.c net_handshake.c slp.c |
| `pc_is_ucf_enabled` | ftCo_Escape.c ftCo_Turn.c slp.c |
| `pc_is_unlock_all_enabled` | gm_1601.c |
| `pc_launcher_configure` | main.c |
| `pc_launcher_run` | main.c |
| `pc_menu_event` | vi.c |
| `pc_menu_init` | main.c |
| `pc_menu_is_open` | keyboard.c vi.c |
| `pc_menu_toggle` | vi.c |
| `pc_menu_update` | vi.c |
| `pc_prefs_get_audio` | Panel.swift |
| `pc_prefs_get_disc` | desktop_melee.c |
| `pc_prefs_get_ucf` | ControlsTab.swift |
| `pc_prefs_save` | ControlsTab.swift Panel.swift |
| `pc_prefs_set_audio` | Panel.swift |
| `pc_prefs_set_ucf` | ControlsTab.swift |
| `pc_set_net_target` | gmonlinemode.c |

## Appendix B. Netplay functions the stub must provide

The 95 symbols left undefined without the netplay sources [build,
experiment 2], grouped by a header that declares them. Offline values for the ones that aren't
zero, false or empty are in section 2.3.

- `src/pc/net_sfx.h`: `net_sfx_is_handle`, `net_sfx_is_private`, `net_sfx_keyoff`, `net_sfx_keyoff_track`, `net_sfx_private`, `net_sfx_resolve`, `net_sfx_set`, `net_sfx_shielded`, `net_sfx_start`.
- `src/pc/net_lan.h`: `pc_lan_connect_direct`, `pc_lan_discovery_unavailable`, `pc_lan_full`, `pc_lan_is_host`, `pc_lan_local_name`, `pc_lan_peers`, `pc_lan_poll`, `pc_lan_seed`, `pc_lan_start`, `pc_lan_start_frame`, `pc_lan_start_match`, `pc_lan_state`, `pc_lan_stop`, `pc_net_recv_reliable`, `pc_net_send_reliable`.
- `src/pc/net.h`: `pc_net_active`, `pc_net_after_tick`, `pc_net_audio_deaf`, `pc_net_audio_deaf_note`, `pc_net_audio_record`, `pc_net_audio_replay`, `pc_net_catch_up_ns`, `pc_net_chat_available`, `pc_net_desync`, `pc_net_deterministic`, `pc_net_frame`, `pc_net_init`, `pc_net_local_player`, `pc_net_note_io`, `pc_net_pace_adjust_ns`, `pc_net_peer_status`, `pc_net_peer_status_clear`, `pc_net_poll`, `pc_net_pure_load`, `pc_net_quality`, `pc_net_render_audit`, `pc_net_resim`, `pc_net_rules`, `pc_net_rumble_command`, `pc_net_scene_hold`, `pc_net_seed`, `pc_net_set_input_delay`, `pc_net_sfx_on`, `pc_net_stats`, `pc_net_sync`.
- `src/pc/net_chat.h`: `pc_net_chat_line`, `pc_net_chat_poll`, `pc_net_chat_prompt`.
- `src/pc/slp.h`: `pc_net_confirmed_frame`, `pc_net_sim_frame`.
- `src/pc/net_internal.h`: `pc_net_disconnect`.
- `src/pc/net_match.h`: `pc_net_match_clipboard_code`, `pc_net_match_contacts`, `pc_net_match_copy_code`, `pc_net_match_identity`, `pc_net_match_local_code`, `pc_net_match_opponent_code`, `pc_net_match_poll`, `pc_net_match_poll_publication`, `pc_net_match_profile_directory`, `pc_net_match_progress`, `pc_net_match_publication`, `pc_net_match_publish_rank`, `pc_net_match_seed`, `pc_net_match_start`, `pc_net_match_start_frame`, `pc_net_match_state`, `pc_net_match_stop`.
- `src/pc/net_rank.h`: `pc_rank_display`.
- `src/pc/net_rank_session.h`: `pc_rank_session_abort`, `pc_rank_session_active`, `pc_rank_session_choose_stage`, `pc_rank_session_game`, `pc_rank_session_poll`, `pc_rank_session_seconds`, `pc_rank_session_set_complete`, `pc_rank_session_stage`, `pc_rank_session_stage_available`, `pc_rank_session_stage_begin`, `pc_rank_session_stage_port`, `pc_rank_session_stage_prompt`, `pc_rank_session_state`, `pc_rank_session_stocks`.
- `src/pc/net_rank_store.h`: `pc_rank_store_close`, `pc_rank_store_current`, `pc_rank_store_open`.
