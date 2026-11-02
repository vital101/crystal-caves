/* Crystal Caves - HUD and text rendering (Phase 5).
 *
 * Fonts are the original ProGraphx MNI banks: 50 glyphs of 40 bytes, each
 * glyph 8 rows x 5 bytes [mask][plane0..plane3], MSB = leftmost pixel.
 * The font pointers passed by the shell point at glyph 0 (the 3-byte MNI
 * header has already been stripped by the asset exporter).
 *
 * Phase 2 split (mechanical).  Module ownership:
 *
 *   font.c      glyph banks, the CS:0x64C character table, F1/F2 selection
 *   window.c    exact window frames/layout/strings, the '^' cursor cell,
 *               block and lines drawing
 *   hud.c       digit formatting, cc_hud_tick cadence, HUD rendering
 *
 * text.h remains the only public API.  Each module keeps its helpers
 * file-local; font.h exposes the shared banks and glyph lookup.
 */
#include "font.h"
#include "window.h"
#include "hud.h"
