# Contributing to Desktop Melee

Bug reports and pull requests are welcome. Desktop Melee is small and run by
one person, so keeping changes focused helps a lot.

## Scope

- **macOS only, for now.** Windows and Linux are planned: melee-pc already
  runs there, and the work is the desktop layer (the transparent window, a
  floor on the taskbar, a tray app instead of the menu bar app). If you want
  to help with that, open an issue first so we can agree on the approach.
- **Melee USA 1.02 only.** The disc check accepts only a clean USA revision 2
  image. Requests for PAL, other revisions or modded images will be closed.
- **The game's behaviour stays Melee's.** Desktop Melee changes the camera,
  the stage and the HUD around the game, never movement, hitboxes or
  knockback.

## Never

- Never attach, link or commit game data: disc images, extracted files,
  textures, sounds, or `DESKTOP_MELEE_DUMP` output.
- Never share an app built by a plain `desktop-melee/package.sh`. It contains
  Melee's font, copied from your own disc. Only release builds
  (`DESKTOP_MELEE_RELEASE=1`) are made to be shared.
- Don't ask for or offer ROMs or ISOs in issues.

## Building and testing

See the README for requirements, then:

```sh
desktop-melee/package.sh             # build, sign and install the app
desktop-melee/tests/check_style.sh   # style; --fix reformats (needs clang-format 18)
desktop-melee/tests/run_all.sh       # every test, about 9 minutes
```

Every pull request runs the style check and a macOS build on GitHub
(`.github/workflows/checks.yml`). The tests need a disc and a screen, so
they don't run there: run the ones your change touches yourself.

- `disc_check_test.sh` and `font_bundle_test.sh` run in the background and
  need your disc image.
- `welcome_window_test.sh`, `island_test.sh` and `tweaks_test.sh` open the
  app on your screen and take it over for a few minutes. They save and
  restore your settings (the `desktop-melee` defaults suite), but back it up
  first: `defaults export desktop-melee backup.plist`.

[docs/desktop-melee/how-it-works.md](docs/desktop-melee/how-it-works.md)
explains the code layer by layer.

## Code style

- **C and C++:** clang-format 18 with the repository's `.clang-format`
  (100 columns); `check_style.sh --fix` does it. **Objective-C:** the same
  100 columns, by hand (`.clang-format` has no Objective-C section).
- **Every new file** starts with `SPDX-License-Identifier: GPL-3.0-or-later`
  in a comment, like the existing ones.
- **Hooks in melee-pc files** stay a few lines long and call into
  `desktop-melee/`. Each one gets a comment that starts with `desktop-melee`,
  so `git grep -n 'desktop-melee' -- src extern` finds them all.
- **Comments explain why**, not what the next line does.
- **Swift:** the menu bar app and the welcome window run on the game loop's
  main thread. Don't use SwiftUI-driven loops (`TimelineView`,
  `.repeatForever`) or blur filters there; run looping motion on Core
  Animation layers instead.

melee-pc's own guide, [docs/melee-pc/CODING_STYLE.md](docs/melee-pc/CODING_STYLE.md),
still applies to its code under `src/` and `extern/`.

## Pull requests

- Keep changes outside `desktop-melee/` small and tagged (see Code style).
  Add any newly changed melee-pc file to the list in `licenses/LICENSE.md`.
- Say how you tested it, and on which macOS version and Mac.

By contributing, you agree that your changes are licensed under
GPL-3.0-or-later, like the rest of Desktop Melee's code.
