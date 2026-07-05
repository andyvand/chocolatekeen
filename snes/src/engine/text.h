/* BG3 text console (M6 UI foundation).
 *
 * Mode 1 BG3, 2bpp: font chars at VRAM $5000, 32x32 tilemap at $4800.
 * Glyph index == Keen font index == ASCII char code (verified against
 * the desktop CVort_draw_string, which passes *str straight to
 * CVort_engine_drawChar; +0x80 selects the dark-background variants
 * used by CVort_draw_string_80). Box border glyphs are 1..8 in the
 * order TL,T,TR,L,R,BL,B,BR (desktop CVort_draw_box2); 9..13 are the
 * animated key-wait cursor.
 *
 * The baked font keeps EGA color 0 as 2bpp color 0. Keen glyphs use
 * color 0 for the letterforms (white paper, black ink), so ck_text_init
 * re-encodes each glyph on upload: color 0 becomes opaque color 3
 * ("ink") and glyph 0 is reserved as the fully transparent filler the
 * cleared shadow map points at.
 *
 * Text colors use 2bpp palettes 4..7 (CGRAM entries 16..31) so BG1's
 * EGA palette 0 (entries 0..15) stays untouched. Writes go to a WRAM
 * shadow map flushed by DMA in ck_text_vblank when dirty.
 *
 * All map entries carry the tile priority bit; with REG_BGMODE bit 3
 * set (setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH), done at boot) the
 * layer draws above BG1 *and* all sprites, like the DOS overlay.
 */
#ifndef CK_SNES_TEXT_H
#define CK_SNES_TEXT_H

#include <snes.h>

#define CK_BG3_CHR_VRAM 0x5000
#define CK_BG3_MAP_VRAM 0x4800

#define CK_TEXT_COLS 32
#define CK_TEXT_ROWS 28

/* color = 0..3 -> 2bpp palette 4..7 */
enum {
    CK_TEXT_WHITE = 0,  /* white paper, black ink (normal glyphs)      */
    CK_TEXT_DARK,       /* dark paper, white letters (+0x80 glyphs)    */
    CK_TEXT_RED,        /* white paper, red ink                        */
    CK_TEXT_GRAY        /* gray paper, black ink                       */
};

/* Upload font + text palettes, clear the shadow, set BG3 pointers.
 * Call under forced blank (title/level init paths do). */
void ck_text_init(void);

void ck_text_clear(void);
void ck_text_char(u8 x, u8 y, u8 glyph, u8 color);
void ck_text_string(u8 x, u8 y, const char *s, u8 color);

/* DOS-style box with the Keen border glyphs, cleared interior. */
void ck_text_box(u8 x, u8 y, u8 w, u8 h, u8 color);

/* Show/hide the whole layer (BG3 enable). */
void ck_text_show(u8 on);

/* Flush shadow to VRAM if dirty; call right after WaitForVBlank. */
void ck_text_vblank(void);

#endif
