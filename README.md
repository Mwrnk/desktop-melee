<h1 align="center"><img src="docs/desktop-melee/images/header.png" alt="Desktop Melee" width="760"></h1>

Super Smash Bros. Melee on your Mac's desktop. The fighters stand on top of
the Dock, the screen edges are the blastzones, and your apps keep working
underneath. Set up the match from the menu bar, plug in a controller, and
play.

![Desktop Melee on a Mac desktop](docs/desktop-melee/images/overlay.svg)

**Beta (0.1.0), macOS only for now.** Windows and Linux are in progress.

**You need your own Melee disc image (USA 1.02). No game data is included.**
Desktop Melee is a fan project. It is not affiliated with or endorsed by
Nintendo or HAL Laboratory.

## What it is

- **The real game.** It runs Melee's own code, built from the community
  decompilation, so movement, hitboxes and knockback are exactly Melee's. It
  is not an emulator.
- **A see-through overlay.** One transparent, click-through window covers the
  screen. Only the fighters, effects and HUD are drawn.
- **Your desktop is the stage.** Final Destination's floor is stretched to the
  width of your Dock, the screen edges are the blastzones, and stocks and
  percents sit in a black "island" around the MacBook notch.
- **A menu bar app.** Pick characters and costumes (with Melee's own
  portraits and announcer), CPU levels, controller ports, button maps, tap
  jump, fighter size, HUD placement and more.
- **Pause and hide.** One shortcut (⌃⌥M by default) pauses the match and
  hides the fighters, for when someone walks by.

## How it works, in one picture

```mermaid
flowchart LR
    disc[("your disc<br/>Melee USA 1.02")] --> game["Melee's game code<br/>(decompiled C, built with GCC)"]
    game --> port["melee-pc + aurora<br/>GameCube hardware → Metal, SDL3"]
    port --> desktop_melee["Desktop Melee<br/>transparent window, Dock floor,<br/>menu bar app"]
    desktop_melee --> desk["your desktop"]
```

The full explanation, layer by layer, with diagrams: **[How Desktop Melee
works](docs/desktop-melee/how-it-works.md)**. It covers decompilation versus
emulation, how the window becomes see-through, the math that maps your screen
into Melee's world, the menu bar app, and every file.

## Download

Each [release](https://github.com/Mwrnk/desktop-melee/releases) has a
`Desktop-Melee-<version>-macOS-arm64.zip`: a Mac with Apple silicon and
**macOS 27 or later**. It contains no game data; you still need your own disc
(see [Requirements](#requirements)).

The app isn't notarized by Apple, so macOS stops it the first time:

1. Unzip it and move **Desktop Melee** to `/Applications`.
2. Open it. macOS says it can't check the app for malware; click **Done**.
3. Open **System Settings → Privacy & Security**, scroll down to "Desktop
   Melee was blocked", and click **Open Anyway**. Confirm with your password.

You only do this once per version. If you granted Accessibility (see
[Permissions](#permissions)), macOS forgets it with each new version: remove
Desktop Melee from the Accessibility list and add it again.

## Requirements

- A Mac with Apple silicon. Tested on macOS 27 only. If you build the app
  yourself, it runs on the macOS of the Mac that built it; older versions and
  Intel Macs are untested. Building needs Xcode 26 or later, for the Icon
  Composer app icon.
- Xcode (for Swift, `actool` and the macOS SDK) and these
  [Homebrew](https://brew.sh) packages:
  `brew install gcc cmake ninja sdl3 zstd libpng freetype`. GCC is required:
  the game code uses a GCC-only feature (see
  [how it works](docs/desktop-melee/how-it-works.md#41-big-endian-data-read-for-free)).
- Your own Super Smash Bros. Melee disc image: **USA, revision 2 (1.02),
  unmodified** (`.iso`, `.gcm`, `.ciso` or `.rvz`). Desktop Melee checks it on first
  launch and refuses other versions.
- A controller is recommended: an official GameCube controller with a USB
  adapter, or any gamepad macOS supports. The keyboard works too.

## Build and run

```sh
git clone https://github.com/Mwrnk/desktop-melee.git
cd desktop-melee
desktop-melee/package.sh
```

`desktop-melee/package.sh` builds everything, makes `Desktop Melee.app`, signs it and
installs it in `/Applications` (set `DESKTOP_MELEE_INSTALL=0` to keep it in
`build/macos`). The first build downloads dependencies and takes a while.

Open **Desktop Melee** from `/Applications`. On first launch the welcome
window asks for your disc, checks it, has you press a button on each
controller, and shows how to play.

**The first launch takes several minutes** ("Getting Melee ready…"): the game
builds all of its graphics shaders once, so matches never stutter. Later
launches take a few seconds. Then click the stage icon in the menu bar
and press **Start**, or press Start on a controller.

To run it from a terminal with the log visible:

```sh
desktop-melee/run-overlay.sh path/to/melee.iso
```

### Permissions

- **Accessibility (optional):** lets Desktop Melee read the Dock's exact size
  and position, so the floor matches it to the point. Without it, the Dock's
  width is estimated from its settings. It is used for nothing else.
- **No network access.** Desktop Melee itself makes no network requests. It
  doesn't use melee-pc's online play or updater.

## Project layout

| Path | What |
|---|---|
| `desktop-melee/` | Desktop Melee: overlay, game mode, menu bar app, packaging. |
| `src/melee/`, `src/sysdolphin/` | Melee's decompiled game code (from doldecomp/melee). |
| `src/pc/` | melee-pc's platform layer. |
| `extern/aurora/` | aurora: GameCube SDK → WebGPU/SDL3. |
| `docs/desktop-melee/` | Desktop Melee's documentation. |
| `MELEE-PC.md` | melee-pc's own README: the port Desktop Melee is built on. |
| `docs/melee-pc/` | melee-pc's docs, roadmap and coding style, kept as they were. |

## Before you share anything

- **Never share an app from a plain `desktop-melee/package.sh`.** It copies
  Melee's font from your disc into the app so the welcome window can use it,
  so that app (or a zip of it) contains Nintendo's data. To make an app you
  can hand out, run `DESKTOP_MELEE_RELEASE=1 desktop-melee/package.sh`: it
  leaves the font out, stops if any other game file is inside, signs ad hoc
  and writes the zip to `dist/`.
- **Never commit game data:** no disc images, no extracted textures, no
  `DESKTOP_MELEE_DUMP` output.
- **Bug reports:** don't attach or link disc images.

## Legal

This is not legal advice.

- **Game code (`src/melee/`, `src/sysdolphin/`) is not licensed.** It is a
  decompilation of a Nintendo game, from
  [doldecomp/melee](https://github.com/doldecomp/melee). Neither this project
  nor melee-pc can grant a license to it. This is the same position as
  doldecomp and melee-pc.
- **Port code and Desktop Melee (`src/pc/`, `desktop-melee/`, tools, build files) are
  GPL-3.0-or-later.** Third-party parts keep their own licenses (MIT, zlib,
  BSD, OFL).
- No game assets are in this repository. Everything is read from your disc
  at runtime. The one thing that comes close is the interface font,
  `desktop-melee/macos/DesktopMelee.ttf`: a vector recreation of Melee's menu
  font, whose letter shapes are Nintendo's design.
- "Super Smash Bros." and "Melee" are trademarks of Nintendo.

The details are in [licenses/LICENSE.md](licenses/LICENSE.md).

## Credits

Desktop Melee stands on the work of:

- [doldecomp/melee](https://github.com/doldecomp/melee): the decompilation
  of Melee.
- [melee-pc](https://github.com/999sian/melee-pc): the native PC port that
  Desktop Melee is a fork of. Its README is kept as [MELEE-PC.md](MELEE-PC.md).
- [aurora](https://github.com/encounter/aurora): the GameCube SDK
  compatibility layer on WebGPU and SDL3.
- [Dawn](https://dawn.googlesource.com/dawn), [SDL3](https://libsdl.org),
  [nod](https://github.com/encounter/nod) and
  [RmlUi](https://github.com/mikke89/RmlUi).

## Status

Version 0.1.0, the first beta: [download the app](#download) or build it
yourself. macOS only for now; Windows and Linux are in progress.

Works: two-player VS matches (human or CPU) on the Dock, every character and
costume, the menu bar app, the welcome window, the notch island and
Pause and hide.

### Known issues

- **The first launch takes about 5 minutes** while the game builds its
  graphics shaders. Later launches take a few seconds.
- **Tested on one Mac** (Apple silicon, macOS 27) **with one screen.** You can
  pick another screen in the Display tab, but that is untested.
- **Timer details:** with the timer at the top, the clock shows during a
  pause (Melee hides it). With the timer hidden, Melee's last-5-seconds
  countdown sound is silent too.
- **Player tags** switched on or off during a pause change only when the
  match resumes.
- **The Pause and hide shortcut** can fire while the welcome window is open.

They are tracked in the [issues](https://github.com/Mwrnk/desktop-melee/issues).

### Roadmap

- Windows and Linux. melee-pc already runs there; the desktop layer (the
  transparent window, a floor on the taskbar, a tray app) is the work.
- Window tops as platforms you can drop through.
- More than two players.
- Other stages.

Bug reports and pull requests are welcome: see
[CONTRIBUTING.md](CONTRIBUTING.md) for the scope and how to test.
