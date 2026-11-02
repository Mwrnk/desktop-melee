/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A dashed border for the Windows and Linux windows' RmlUi documents, which
 * RmlUi doesn't have:
 *
 *   decorator: dashed(1.5dp 6dp 5dp);    width, dash, gap
 *   image-color: #ffffff40;              the colour (it can be animated)
 *
 * It follows the element's border-radius, like the macOS app's dashed
 * strokeBorder (the disc drop zone, the empty player slots). */
#pragma once

/* Registers "dashed" with RmlUi. Call after Rml::Initialise and before
 * loading a document that uses it; later calls do nothing. */
void desktop_melee_rml_dashed_register();
