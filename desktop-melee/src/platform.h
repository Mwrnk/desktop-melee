/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What Desktop Melee needs from the operating system, beyond what SDL offers.
 *
 * Each system implements every function here, in one file:
 *   macOS    desktop-melee/src/overlay_mac.m (the reference; the menu bar app
 *            it forwards to is SwiftUI, in desktop-melee/ui)
 *   Windows  desktop-melee/src/platform_windows.c (skeleton)
 *   Linux    desktop-melee/src/platform_linux.c (skeleton; also the plain SDL
 *            fallback for any other system)
 * desktop-melee/desktop-melee.cmake picks the file. docs/desktop-melee/porting.md
 * says what each function needs on each system.
 *
 * Units. Sizes and positions are in points: the units SDL uses for window
 * and display coordinates (SDL_GetDisplayBounds), not pixels. On a 2x
 * display, a 1512x982 point screen is 3024x1964 pixels. Unless a function
 * says otherwise, x runs right from the left edge of the overlay's screen
 * and y runs UP from its bottom edge.
 *
 * Threads. Everything is called on the main thread (the one that created
 * the SDL window), and none of it may block for long: the game loop runs
 * on that thread too.
 *
 * desktop_melee.c only calls these while the overlay is on
 * (DESKTOP_MELEE_OVERLAY=1), except desktop_melee_platform_write_png, which
 * asset dumps (DESKTOP_MELEE_DUMP) use in any mode. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include <SDL3/SDL_video.h>
#include <aurora/aurora.h>

/* ---- the overlay window ------------------------------------------------- */

/* Before the window exists: adjust aurora's settings. desktop_melee.c has
 * already asked for config->overlayWindow, which makes aurora create a
 * borderless, transparent, always-on-top window covering the primary
 * display.
 *   macOS: nothing to change.
 *   Skeletons: turn overlayWindow off. Until the window lets clicks through
 *   (desktop_melee_platform_overlay_setup), a topmost full-screen window
 *   locks the player out of their own desktop. The game then runs in a
 *   normal window, with the fighters on a plain background. */
void desktop_melee_platform_configure(AuroraConfig* config);

/* Once, right after aurora creates the window: everything SDL has no API
 * for that makes it an overlay. desktop_melee.c then moves it to the chosen
 * screen and calls desktop_melee_platform_ui_install.
 *   macOS: click-through (ignoresMouseEvents, kept on by a guard, since SDL
 *   turns it off), no shadow, a clear background, the level just below the
 *   menu bar, on every Space and beside fullscreen apps, out of Cmd-`
 *   cycling. Starts the once-a-second desktop_melee_trace_tick timer when
 *   DESKTOP_MELEE_TRACE=1 (the tests use it).
 *   Required of a port: clicks, drags and scrolls always reach the windows
 *   underneath; the window never takes focus by itself; it stays above
 *   normal windows and below the system's own bars. */
void desktop_melee_platform_overlay_setup(SDL_Window* window);

/* Whether desktop_melee.c may move and resize the window to cover the
 * chosen screen (desktop_melee_set_screen). True for a real overlay; false
 * for a skeleton's normal window, which keeps the size the player gives it.
 *   macOS: true. */
bool desktop_melee_platform_window_covers_screen(void);

/* Take the overlay off the screen, or put it back. The welcome window hides
 * it while it waits for a disc: nothing is drawn yet, and a window covering
 * the screen only gets in the way. Called from the UI.
 *   macOS: orderOut / orderFrontRegardless (and click-through again). */
void desktop_melee_platform_overlay_set_hidden(bool hidden);

/* Pause and hide, and while the launcher waits for shaders: the overlay
 * goes fully transparent but keeps drawing, so the game loop never stalls
 * on a hidden or minimised window.
 *   macOS: alphaValue 0 or 1. */
void desktop_melee_platform_overlay_set_faded(bool faded);

/* ---- the screen --------------------------------------------------------- */

/* The overlay's screen: its size, and the floor, the height of the top of
 * the taskbar or Dock along the bottom of the screen (0 when there is none,
 * or when it hides or sits on a side). Called at startup, whenever the
 * overlay changes screens, and when the player asks to measure again.
 *   macOS: NSScreen.frame for the size; the floor is how far
 *   visibleFrame's bottom sits above frame's.
 *   Skeletons: the window's own size, floor 0. */
void desktop_melee_platform_screen_geometry(
    SDL_Window* window, float* width_pt, float* height_pt, float* floor_pt);

/* The taskbar or Dock along the bottom of the overlay's screen: its left
 * and right edges and its top, in points. Its span becomes the stage's
 * floor; outside it, fighters fall. Returns false when there is none on
 * this screen (the floor is then Final Destination's own). *exact says
 * whether the frame is measured (desktop_melee.c then uses *top as the
 * floor) or estimated (the floor stays desktop_melee_platform_screen_geometry's).
 *   macOS: the Dock's list element through the Accessibility API (exact,
 *   once the player grants Accessibility), else an estimate from the
 *   Dock's settings: icon size times the icons it shows, centred.
 *   Skeletons: false. */
bool desktop_melee_platform_dock_frame(
    float screen_w, float screen_h, float* left, float* right, float* top, bool* exact);

/* The notch (camera housing) at the top of the overlay's screen, where the
 * island showing stocks and percents goes. *left and *right are measured
 * from the screen's left edge; *height is the island's height, down from
 * the top edge. Height 0 means no island: stocks and percents stay where
 * Melee puts them.
 *   macOS: NSScreen.auxiliaryTopLeftArea / auxiliaryTopRightArea and
 *   safeAreaInsets.top; on a screen without a notch, left == right at the
 *   centre and the height is the menu bar's.
 *   Skeletons: height 0. */
void desktop_melee_platform_notch(float* left, float* right, float* height);

/* The island: a black shape around the notch, behind the overlay, that
 * stocks and percents are drawn on. Created on the first call, moved by
 * later ones; never called when the notch height is 0. Same units as
 * desktop_melee_platform_notch. While it exists, the platform polls
 * desktop_melee_island_wanted() (about ten times a second, main thread)
 * and fades it in or out.
 *   macOS: a borderless window just below the overlay and the menu bar,
 *   square top, rounded bottom corners (45% of its height), 0.25 s fades.
 *   Skeletons: nothing. */
void desktop_melee_platform_island_place(float left, float right, float height);

/* ---- the settings UI ---------------------------------------------------- */
/* docs/desktop-melee/ui-guide.md describes what the macOS app shows. The UI
 * talks to the game through desktop-melee/ui/bridge/desktop_melee_bridge.h. */

/* Once, after desktop_melee_platform_overlay_setup: the menu (macOS's menu
 * bar item and its popover), the welcome window on first run, and the
 * global Pause and hide hotkey, which calls
 * desktop_melee_toggle_pause_and_hide() from any app.
 *   macOS: desktop_melee_ui_install (desktop-melee/ui/MenuBar.swift) with
 *   the window's NSWindow; makes the process an accessory app (no Dock
 *   icon).
 *   Skeletons: nothing; settings keep their defaults. */
void desktop_melee_platform_ui_install(SDL_Window* window);

/* After Pause and hide comes back during a match: make the game window key,
 * so keyboard players press Start in the game and not in whatever app is in
 * front.
 *   macOS: activates the app and makes the overlay key. */
void desktop_melee_platform_ui_focus_game(void);

/* Around melee-pc's launcher, which waits for the graphics pipelines to
 * build. The overlay is faded meanwhile; the UI shows progress, polling
 * desktop_melee_pipelines_pending().
 *   macOS: a small loading window (desktop-melee/ui/Loading.swift). */
void desktop_melee_platform_ui_loading_begin(void);
void desktop_melee_platform_ui_loading_end(void);

/* The disc image to boot. candidate is the command line's disc, else the
 * one melee-pc's launcher booted last; it may be NULL or "". Only an image
 * that passes the disc check (disc_check.h: a clean Melee USA 1.02) may be
 * returned. NULL quits the game. May block while the player chooses: the
 * game has not started yet.
 *   macOS: the welcome window, or a remembered disc that already passed
 *   the check (desktop-melee/ui/DiscMemory.swift).
 *   Skeletons: the candidate if it passes the check, run on the spot. */
const char* desktop_melee_platform_ui_choose_disc(const char* candidate);

/* The game has decoded the roster, portraits and font from the disc
 * (desktop-melee/src/assets.c): the UI may draw them now.
 *   macOS: desktop-melee/ui/GameAssets.swift builds its images. */
void desktop_melee_platform_ui_assets_ready(void);

/* ---- files -------------------------------------------------------------- */

/* Writes straight (not premultiplied) RGBA8 pixels, rows top to bottom, as
 * a PNG. Only for DESKTOP_MELEE_DUMP, which finds assets on the disc; the
 * files are Nintendo's and never go in the repository.
 *   macOS: ImageIO. Skeletons: SDL_SavePNG. */
void desktop_melee_platform_write_png(const char* path, const uint8_t* rgba, int width, int height);
