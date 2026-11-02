/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Melee's own font in the Windows and Linux windows' RmlUi documents, as the
 * macOS app draws its text (ui/GameAssets.swift, MeleeFont): the font family
 * "Melee" is drawn from the game's 32x32 glyphs, read from the player's disc.
 * Every other family goes to RmlUi's own font engine.
 *
 * The glyphs are cached in the settings folder as melee-font.bin (the macOS
 * app's format, see tests/font_bundle_test.sh), so later launches have the
 * font before the disc loads. Until there are glyphs, "Melee" is drawn in
 * Noto Sans. Never commit melee-font.bin: it is the disc's. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Puts the font engine in front of RmlUi's, and reads melee-font.bin if
 * there is one. Call after Rml::Initialise, before loading a document that
 * uses "Melee"; later calls do nothing. */
void desktop_melee_font_install(void);

/* Once the game's assets are ready: reads the glyphs from the game if there
 * were none, saves melee-font.bin, and redraws all text in Melee's font. */
void desktop_melee_font_assets_ready(void);

#ifdef __cplusplus
}
#endif
