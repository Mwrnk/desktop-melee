# Porting Desktop Melee to Windows and Linux

Desktop Melee runs on macOS only today. melee-pc, the port underneath it,
already runs on Windows and Linux, so the game itself builds and plays there.
What's missing is the desktop layer: the see-through, click-through window,
the floor on the taskbar, the hotkey and the settings UI. This page is for
whoever takes on that work. Windows is issue #11 and Linux is issue #12.

Read [how-it-works.md](how-it-works.md) first, sections 8 to 10 especially,
for what the macOS version does and why. [ui-guide.md](ui-guide.md) describes
the settings UI a port has to match.

Contents:

1. [Where things stand](#1-where-things-stand)
2. [Building and running a skeleton](#2-building-and-running-a-skeleton)
3. [The platform interface](#3-the-platform-interface)
4. [Windows](#4-windows)
5. [Linux on X11](#5-linux-on-x11)
6. [Linux on Wayland](#6-linux-on-wayland)
7. [The settings UI](#7-the-settings-ui)
8. [What else a port touches](#8-what-else-a-port-touches)
9. [Suggested order](#9-suggested-order)
10. [Ground rules](#10-ground-rules)

---

## 1. Where things stand

Everything that is specific to an operating system goes through one header,
[`desktop-melee/src/platform.h`](../../desktop-melee/src/platform.h). Each
system implements it in one file:

| System | File | State |
|---|---|---|
| macOS | [`overlay_mac.m`](../../desktop-melee/src/overlay_mac.m), plus the SwiftUI app in [`desktop-melee/ui/`](../../desktop-melee/ui/) | complete |
| Windows | [`platform_windows.c`](../../desktop-melee/src/platform_windows.c) | skeleton |
| Linux | [`platform_linux.c`](../../desktop-melee/src/platform_linux.c) | skeleton; plain SDL, so CMake also uses it for any other system |

[`desktop-melee.cmake`](../../desktop-melee/desktop-melee.cmake) picks the
file. Every pull request builds all three (`.github/workflows/checks.yml`).

The skeletons compile and run, with fallbacks:

- **A normal window.** aurora's overlay mode makes a borderless, topmost
  window that covers the screen. Until it lets clicks through, that window
  would lock you out of your own desktop, so the skeletons turn overlay mode
  off (`desktop_melee_platform_configure`). The game runs in a normal,
  resizable window, with the fighters on a plain background.
- **The floor is the window's bottom edge.** No taskbar measuring yet.
- **No notch island.** Stocks and percents stay in Melee's own HUD.
- **Pause and hide does nothing.** There's no hotkey and no menu to press it
  from.
- **No settings UI.** Every setting keeps its default (Fox vs Fox, 4 stocks,
  7 minutes).
- **The disc comes from the command line**, and must pass the same check as
  on macOS: a clean Melee USA 1.02 image.

Each function in the skeletons has a TODO naming the system APIs a port would
use. The rest of this page goes through them.

## 2. Building and running a skeleton

The setup is melee-pc's: see `.github/workflows/checks.yml` for the exact
packages and commands CI uses, and
[docs/melee-pc/building.md](../melee-pc/building.md) for the details.

**Linux** (Ubuntu 24.04; install the `apt-get` list from the Linux job in
`checks.yml` first):

```bash
cmake -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DAURORA_SDL3_PROVIDER=vendor -DAURORA_SDL3_LINKAGE=shared -DAURORA_DAWN_PROVIDER=package -DAURORA_NOD_PROVIDER=package
```

```bash
ninja -C build/linux melee
```

```bash
DESKTOP_MELEE_OVERLAY=1 build/linux/melee --dvd /path/to/melee.iso
```

**Windows**: CI cross-compiles from Linux with MinGW-w64
(`TARGET_ARCH=x86_64 tools/package_windows.sh`, which also builds libusb),
and the result lands in `dist/`. Building on Windows itself follows melee-pc's
instructions. Run it with:

```bat
set DESKTOP_MELEE_OVERLAY=1
melee.exe --dvd C:\path\to\melee.iso
```

Without `DESKTOP_MELEE_OVERLAY=1` you get plain melee-pc, with its own
launcher. With it, the log should show `desktop-melee: Linux skeleton: a
normal window` (or Windows), the disc check, then the idle scene, which
draws nothing. Press Start on a controller (or the keyboard's Start key) to
start a match.

Useful environment variables, all from `desktop_melee.c`:

| Variable | Effect |
|---|---|
| `DESKTOP_MELEE_OVERLAY=1` | Desktop Melee instead of melee-pc |
| `DESKTOP_MELEE_SCALE=<units>` | fighter size, as Melee units across the screen (490 is 100%) |
| `DESKTOP_MELEE_TRACE=1` | once-a-second trace lines the tests read (needs the platform's timer, see section 8) |
| `DESKTOP_MELEE_INPUT_TRACE=1` | logs tap jumps and remapped buttons |
| `DESKTOP_MELEE_DUMP=<dir>` | writes every texture found on the disc as a PNG. **Never commit these.** |

## 3. The platform interface

`platform.h` documents each function: what it does, its units, what macOS
does, and what's required of a port. In short:

- **Units are points**: SDL's window and display coordinates. On macOS a
  point is a logical unit (two pixels on a Retina screen); on Windows and
  X11, SDL's coordinates are pixels. The math in `desktop_melee.c` only
  needs every function to use the same units. Win32 and X11 calls return
  pixels, and SDL's coordinates on those systems are pixels too.
- **y runs up from the bottom** of the overlay's screen, except in the notch
  functions, which measure down from the top.
- **Everything runs on the main thread**, the one with the game loop, and
  nothing may block, except `choose_disc` before the game starts.

| Function | macOS | Windows | X11 | Wayland |
|---|---|---|---|---|
| `configure` | nothing | keep `overlayWindow` once click-through works | same | same; the layer-shell role may need setting here |
| `overlay_setup` | click-through, level, Spaces | `WS_EX_LAYERED \| WS_EX_TRANSPARENT`, `WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`; per-pixel alpha (section 4) | ARGB visual + compositor, XShape input region, EWMH states | layer-shell surface, empty input region; GNOME: none |
| `window_covers_screen` | true | true once an overlay | same | same |
| `overlay_set_hidden` | `orderOut` | `ShowWindow` | `XUnmapWindow` / `SDL_HideWindow` | unmap the surface |
| `overlay_set_faded` | `alphaValue` | `SetLayeredWindowAttributes(..., LWA_ALPHA)` | `_NET_WM_WINDOW_OPACITY` / `SDL_SetWindowOpacity` | draw nothing (no opacity protocol everywhere) |
| `screen_geometry` | `NSScreen.frame`, `visibleFrame` | `GetMonitorInfoW`: `rcMonitor`, `rcWork` | `SDL_GetDisplayUsableBounds` (`_NET_WORKAREA`), panel struts | output size; exclusive zones or 0 |
| `dock_frame` | the Dock through Accessibility | `SHAppBarMessage(ABM_GETTASKBARPOS)` | the `_NET_WM_WINDOW_TYPE_DOCK` window's geometry | not visible to clients; assume a full-width floor |
| `notch` | `auxiliaryTopLeftArea`, `safeAreaInsets` | none: height 0 | none: height 0 | none: height 0 |
| `island_place` | a borderless black window | (only with an island) | (only with an island) | (only with an island) |
| `ui_install` | the SwiftUI menu bar app, Carbon hotkey | tray, settings, `RegisterHotKey` | tray, settings, `XGrabKey` | tray, settings, GlobalShortcuts portal |
| `ui_focus_game` | activate, `makeKey` | `SetForegroundWindow` | `SDL_RaiseWindow` / `_NET_ACTIVE_WINDOW` | xdg-activation token |
| `ui_loading_begin` / `_end` | a small progress window | a progress window | same | same |
| `ui_choose_disc` | the welcome window | welcome window, `SDL_ShowOpenFileDialog` | same (xdg-desktop-portal) | same |
| `ui_assets_ready` | builds the UI's images | the UI may draw portraits | same | same |
| `write_png` | ImageIO | `SDL_SavePNG` (done) | `SDL_SavePNG` (done) | `SDL_SavePNG` (done) |

The macOS-only Accessibility calls (`desktop_melee_ax_trusted`,
`desktop_melee_ax_request`) are not part of the interface: only the Swift UI
calls them, to measure the Dock exactly. Windows and Linux can measure the
taskbar without asking.

## 4. Windows

**The window** (`overlay_setup`). Get the `HWND` with
`SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL)`.

- **Click-through**: add `WS_EX_LAYERED | WS_EX_TRANSPARENT` to the extended
  style (`GetWindowLongPtrW` / `SetWindowLongPtrW` with `GWL_EXSTYLE`). With
  both set, hit-testing skips the window entirely, so clicks reach whatever
  is underneath. Give a layered window `SetLayeredWindowAttributes(hwnd, 0,
  255, LWA_ALPHA)` or it won't show at all.
- **Never activated, not in the taskbar or Alt-Tab**: `WS_EX_TOOLWINDOW |
  WS_EX_NOACTIVATE`.
- **Topmost**: aurora's `SDL_WINDOW_ALWAYS_ON_TOP` is already `HWND_TOPMOST`.
  The taskbar is topmost as well; check which wins, since the fighters stand
  on the taskbar and shouldn't be hidden behind it.
- **Per-pixel alpha is the hard part.** aurora asks the WebGPU surface for
  premultiplied alpha. A plain `HWND` swap chain (flip model, D3D12 or
  Vulkan) usually offers only opaque, and aurora then logs `Overlay window:
  surface has no premultiplied alpha mode; it will be opaque`. The fighters
  then sit on black over the whole screen. Ways out, best first:
  1. **DirectComposition.** A swap chain created for composition
     (`CreateSwapChainForComposition`, `DXGI_ALPHA_MODE_PREMULTIPLIED`)
     attached to an `IDCompositionVisual` on the `HWND`. Dawn would have to
     create its surface that way; look at what Dawn's D3D12 backend supports
     before writing anything.
  2. **Copy each frame to a layered window** with `UpdateLayeredWindow`. It
     works everywhere but costs a GPU-to-CPU copy of a full-screen frame 60
     times a second.
  3. **A colour key** (`LWA_COLORKEY`): one colour becomes transparent. Cheap,
     but edges and effects lose their alpha. Fine as a first step only.

**Pause and hide** (`overlay_set_faded`): `SetLayeredWindowAttributes(hwnd, 0,
faded ? 0 : 255, LWA_ALPHA)`. Keep drawing: a hidden or minimised window can
stall presentation, and with it the game loop.

**The floor** (`screen_geometry`, `dock_frame`):

- `MonitorFromWindow` and `GetMonitorInfoW`: `rcMonitor` is the screen,
  `rcWork` is what's left after the taskbar. The floor is
  `rcMonitor.bottom - rcWork.bottom`. (`SDL_GetDisplayBounds` and
  `SDL_GetDisplayUsableBounds` report these same rectangles.)
- `SHAppBarMessage(ABM_GETTASKBARPOS, &data)` gives the primary taskbar's
  rectangle in `data.rc` and its edge in `data.uEdge`. Only `ABE_BOTTOM` is a
  floor. `ABM_GETSTATE` returns `ABS_AUTOHIDE` for an auto-hiding taskbar,
  which should count as no floor, as a hidden Dock does on macOS. Taskbars on
  other monitors are `Shell_SecondaryTrayWnd` windows (`FindWindowExW`).
- The Windows 11 taskbar spans the whole width, so the floor does too. That's
  fine: return its full span, with `exact = true`.
- Convert to the overlay's monitor, with y up from its bottom:
  `top = monitor.bottom - data.rc.top`, `left = data.rc.left - monitor.left`.

**The hotkey** (in `ui_install`): `RegisterHotKey(hwnd, id, MOD_CONTROL |
MOD_ALT | MOD_NOREPEAT, 'M')`, the same default as macOS. `WM_HOTKEY`
arrives in the thread's message queue; `SDL_SetWindowsMessageHook` lets you
see it before SDL does. `RegisterHotKey` fails when another app holds the
combination: say so in the UI, as macOS shows "Taken". Then call
`desktop_melee_toggle_pause_and_hide()`.

**Focus** (`ui_focus_game`): `SetForegroundWindow` works only when the
process received the last input event. The `WM_HOTKEY` counts, so call it
from the hotkey handler.

**DPI**: SDL makes the process per-monitor DPI aware, so Win32 rectangles and
SDL's window coordinates are both in pixels. Don't mix in DPI-scaled values
from elsewhere.

## 5. Linux on X11

Check `SDL_GetCurrentVideoDriver()`: one Linux build runs on both X11 and
Wayland. Get the X11 handles with `SDL_PROP_WINDOW_X11_DISPLAY_POINTER` and
`SDL_PROP_WINDOW_X11_WINDOW_NUMBER`.

**Transparency**: `SDL_WINDOW_TRANSPARENT` makes SDL pick an ARGB (32-bit)
visual. The alpha only shows if a **compositor** runs: one owns the
`_NET_WM_CM_S<screen>` selection (`XGetSelectionOwner`). Most desktops run
one (KDE, GNOME on X11, Xfce with compositing on); bare window managers often
don't. Without one, say so in the log and stay a normal window. Also check
that the WebGPU surface (Vulkan on Linux) reports premultiplied alpha: aurora
logs it when it doesn't.

**Click-through**: give the window an empty input shape, with the XShape
extension from libXext:

```c
XShapeCombineRectangles(display, window, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
```

or `XFixesSetWindowShapeRegion` with an empty region. Clicks then go to the
windows underneath. SDL may reset the shape when the window changes, so
watch for that, as macOS has to guard `ignoresMouseEvents`.

**Stacking and focus**, through EWMH `_NET_WM_STATE`: `_NET_WM_STATE_ABOVE`,
`_NET_WM_STATE_STICKY` (on every workspace), `_NET_WM_STATE_SKIP_TASKBAR`,
`_NET_WM_STATE_SKIP_PAGER`. A window type of `_NET_WM_WINDOW_TYPE_DOCK` or
`_NET_WM_WINDOW_TYPE_NOTIFICATION` keeps most window managers from giving it
focus or decorations. Test against several, because they differ.

**Pause and hide**: `SDL_SetWindowOpacity` sets `_NET_WM_WINDOW_OPACITY`,
which the compositor honours.

**The floor**:

- `SDL_GetDisplayUsableBounds` already reads `_NET_WORKAREA`, so the floor is
  how far the usable area stops above the screen's bottom.
- `_NET_WORKAREA` is one rectangle for all monitors together. On multi-monitor
  setups, read each panel's `_NET_WM_STRUT_PARTIAL` instead.
- `dock_frame`: find the panel window along the bottom edge
  (`_NET_WM_WINDOW_TYPE_DOCK`, found by walking `XQueryTree` from the root, or
  listed by `_NET_CLIENT_LIST` on some window managers) and use its geometry.
  Most Linux panels span the whole width; GNOME's dash and Plank-style docks
  don't, and they make the best floors.

**The hotkey**: `XGrabKey` on the root window for the keycode and modifiers,
grabbed four times over to cover Num Lock (`Mod2Mask`) and Caps Lock
(`LockMask`), since X11 counts them as modifiers. `KeyPress` events then
arrive however the focus lies. Read them from the same `Display`, or open a
second connection and poll it each frame.

## 6. Linux on Wayland

Wayland gives clients much less control on purpose. What works depends on
the compositor:

| | KDE Plasma | wlroots (Sway, Hyprland, ...) | GNOME |
|---|---|---|---|
| An overlay above every window | layer-shell | layer-shell | **no**: stay a normal window |
| Click-through | empty input region | empty input region | empty input region (inside a normal window it does little) |
| Global hotkey | GlobalShortcuts portal | portal where available, or the compositor's own bindings | GlobalShortcuts portal in recent releases |
| Tray icon | yes | depends on the bar | only with the AppIndicator extension |

**The overlay**: the `wlr-layer-shell-unstable-v1` protocol
(`zwlr_layer_shell_v1`). Put the surface on the `OVERLAY` layer, anchor it to
all four edges, set keyboard interactivity to none and the exclusive zone to
-1 or 0 so panels don't move for it. SDL makes its own `xdg_toplevel` for
the window, so the layer surface needs SDL's custom-role window
(`SDL_PROP_WINDOW_CREATE_WAYLAND_SURFACE_ROLE_CUSTOM_BOOLEAN`), which has to
be set when the window is created, in aurora's `window.cpp`. That's a
`desktop-melee` hook in aurora, so keep it small and tag it.

**Click-through**: `wl_surface_set_input_region` with an empty `wl_region`
(`SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER`), then commit.

**The floor**: clients can't see other clients' surfaces, so the panel's
frame is unknown. Layer-shell reports the size you get after other surfaces'
exclusive zones, which tells you how much a bottom panel takes. Otherwise use
a full-width floor at the bottom edge, which is a reasonable game anyway.

**The hotkey**: the `org.freedesktop.portal.GlobalShortcuts` portal over
D-Bus (`CreateSession`, `BindShortcuts`, then the `Activated` signal). The
user confirms the binding in a system dialog, and may pick a different key,
so the UI should show what was bound.

**Focus**: a Wayland client can't take focus by itself. `xdg-activation-v1`
needs a token, which the portal's `Activated` signal can provide.

**GNOME** has no layer-shell and no plan for one, so there's no overlay
there. A normal window (what the skeleton does today) is the honest
fallback. A GNOME Shell extension could do it, but would be a separate
project.

## 7. The settings UI

[ui-guide.md](ui-guide.md) describes every screen of the macOS app, with each
control's range, default, setting and C call, and separates what any port
must do from what is Mac styling. Section 14 there proposes an approach for
Windows and Linux: an SDL tray icon for the menu, an RmlUi settings window,
and a settings file the C side reads instead of UserDefaults. That's a
proposal, not a decision: discuss it in the issue before building it.

A port can land its window work first and the UI later. Until a UI exists,
`ui_install` can at least register the Pause and hide hotkey, which makes the
overlay usable.

## 8. What else a port touches

- **The trace timer.** On macOS, `overlay_setup` starts a once-a-second timer
  that calls `desktop_melee_trace_tick()` when `DESKTOP_MELEE_TRACE=1`. The
  tests read its log lines and send `SIGUSR1` to press Pause and hide
  (Windows has no `SIGUSR1`; `desktop_melee.c` skips it there). A port that
  wants the tests needs the same timer on the main thread.
- **The tests** in `desktop-melee/tests/` drive the installed macOS app
  (`/Applications/Desktop Melee.app`, `defaults`, `pgrep`). Only
  `disc_check_test.sh` and `check_style.sh` are portable today. Ports will
  need their own runners; keep them in `desktop-melee/tests/`.
- **Packaging.** `desktop-melee/package.sh` builds the macOS `.app`. melee-pc's
  `tools/package_windows.sh` and `tools/package_linux.sh` show how melee-pc
  packages (a zip; an AppImage); Desktop Melee's packages would add
  `DESKTOP_MELEE_OVERLAY=1` (a launcher script, a shortcut, or a check for the
  executable's name) and its own name and icon.
- **Logs.** macOS writes `~/Library/Logs/Desktop Melee.log` when started
  without a terminal. Windows has no console for a GUI app; use a log file in
  `SDL_GetPrefPath`.
- **Settings.** UserDefaults is macOS only. See the settings-file proposal in
  ui-guide.md section 14.

## 9. Suggested order

Each step is a pull request that works on its own:

1. **Click-through, transparent overlay.** `configure` keeps `overlayWindow`,
   `overlay_setup` makes the window click-through, and `window_covers_screen`
   returns true. Done when you can use your desktop normally while fighters
   play over it. On Windows, solve per-pixel alpha first: without it, keep
   the normal window.
2. **The floor.** `screen_geometry` and `dock_frame` measure the taskbar or
   panel. Done when Fox lands on top of it and falls off its ends.
3. **Pause and hide.** `overlay_set_faded`, the hotkey in `ui_install`, and
   `ui_focus_game`.
4. **The disc and loading.** `ui_choose_disc` with a file dialog and a
   remembered disc; `ui_loading_begin` / `_end`.
5. **The settings UI**, after agreeing on the approach in the issue.

## 10. Ground rules

These are the same as [CONTRIBUTING.md](../../CONTRIBUTING.md); they matter
more here:

- **macOS must not change.** A port touches `platform_windows.c` or
  `platform_linux.c`, and only adds to shared code. Every pull request
  builds all three systems.
- **Hooks in melee-pc and aurora stay small**, each tagged with a
  `desktop-melee` comment and listed in
  [`licenses/LICENSE.md`](../../licenses/LICENSE.md).
- **No game data in the repository**: no disc images, no
  `DESKTOP_MELEE_DUMP` output, no portraits, icons or fonts from the disc, and
  no screenshots that show them.
- **Melee USA 1.02 only.** The disc check is the same on every system.
