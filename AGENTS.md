# Notes for coding agents

Desktop Melee runs the real Super Smash Bros. Melee (the doldecomp code, through
the melee-pc port, aurora and SDL3) as a transparent overlay on the desktop.
It is macOS only today; Windows and Linux ports are open for help.
[CONTRIBUTING.md](CONTRIBUTING.md) has the full rules. This page is the short
version, plus what agents tend to miss.

## Where things are

- `desktop-melee/`: all of Desktop Melee. `desktop-melee/src/` is the C side,
  with `desktop-melee/src/platform.h` as the per-system interface;
  `desktop-melee/ui/` is the macOS SwiftUI app, `desktop-melee/game/` is
  game-side C (built with GCC), and `desktop-melee/tests/` holds the tests.
- The top-level `src/` and `extern/`: melee-pc, aurora and the decomp. Not
  ours: only small hooks go here.
- Porting to Windows or Linux: read
  [docs/desktop-melee/porting.md](docs/desktop-melee/porting.md) first, and
  [docs/desktop-melee/ui-guide.md](docs/desktop-melee/ui-guide.md) for the UI.
  Platform work goes in `desktop-melee/src/platform_windows.c` or
  `platform_linux.c`.
- How it all works: [docs/desktop-melee/how-it-works.md](docs/desktop-melee/how-it-works.md).

## Rules

- **No game data, ever.** Never create, commit or attach disc images,
  extracted files, textures, sounds, the font, portraits or icons from the
  disc, `DESKTOP_MELEE_DUMP` output, or a built `Desktop Melee.app`. Running
  `DESKTOP_MELEE_DUMP=<dir>` locally is fine; point it at a folder outside the
  repository. Stage files by path, not with `git add -A`.
- **The macOS app must behave the same** after a port's pull request. Shared
  files may change, as long as macOS builds and works as before. Every pull
  request builds macOS, Linux and Windows in CI.
- **Hooks in the top-level `src/` or `extern/`** are a few lines that call into
  `desktop-melee/`. Each gets a comment starting with `desktop-melee`, and each
  newly changed file gets listed in `licenses/LICENSE.md`.
- **Melee USA 1.02 only.** Don't loosen the disc check.
- **Swift UI code runs on the game loop's main thread**: no `TimelineView`,
  `.repeatForever` or blur filters. Put looping motion on Core Animation
  layers.
- **New environment variables** are named `DESKTOP_MELEE_*`, read in
  `desktop_melee.c` or the platform file, and listed in the table in
  porting.md section 2.
- **Git**: pull requests go to `main` on
  [Mwrnk/desktop-melee](https://github.com/Mwrnk/desktop-melee), from a branch
  on your own fork. There is one long-lived branch; releases are tags. This
  repository is not a GitHub fork of melee-pc: a remote pointing at
  `999sian/melee-pc` (often called `upstream` here) is the wrong place for
  Desktop Melee changes. Never push to it.

## Building and testing

On macOS, build with:

```bash
desktop-melee/build.sh
```

The style check, on any system:

```bash
desktop-melee/tests/check_style.sh
```

The style check needs clang-format 18 (`python3 -m pip install
clang-format==18.1.8`); `--fix` reformats. It covers the C and C++ in
`desktop-melee/` and melee-pc's top-level `src/pc/`. Nothing checks
Objective-C or `extern/` (aurora): keep those to 100 columns and the
surrounding style by hand.

Linux and Windows build commands are in porting.md section 2. Those systems
have no automated tests yet: build, run the game as porting.md describes, and
say in the pull request exactly what you checked.

**The macOS tests take over the screen.** `welcome_window_test.sh`,
`island_test.sh`, `tweaks_test.sh` and `run_all.sh` open the installed app and
drive it for minutes. They need `desktop-melee/package.sh` first (which
replaces `/Applications/Desktop Melee.app`) and the player's disc. Ask before
running them, and back up the settings first:
`defaults export desktop-melee <file>`. `disc_check_test.sh` runs without the
screen.

## Pull requests

There is no pull request template. Say what changed, which system and version
you tested on, what you ran, and what you did not test. Don't claim results
you didn't see. For a port, say in issue #11 (Windows) or #12 (Linux) what you
are taking on before you start.
