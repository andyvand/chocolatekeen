#include "engine/text.h"
#include "data_format.h"

/* Keen font border glyphs (desktop CVort_draw_box2): 1..8 in the order
 * TL,T,TR,L,R,BL,B,BR; 0x20 space. */
#define GLYPH_TL 1
#define GLYPH_T  2
#define GLYPH_TR 3
#define GLYPH_L  4
#define GLYPH_R  5
#define GLYPH_BL 6
#define GLYPH_B  7
#define GLYPH_BR 8

static u16 s_shadow[CK_TEXT_COLS * CK_TEXT_ROWS];
static u8 s_dirty;

/* 2bpp palettes 4..7 (CGRAM 16..31). The baked glyphs use color 1 for
 * "paper" (EGA white), 2 for a secondary color, and - after the upload
 * transform - 3 for "ink" (EGA black letterforms). bgr555.
 *   WHITE: white paper, black ink (DOS menu/box look)
 *   DARK : dark paper, white ink (+0x80 glyph variants / highlights)
 *   RED  : white paper, red ink
 *   GRAY : gray paper, black ink (status shading)
 */
static const u16 s_textPals[16] = {
    0x0000, 0x7FFF, 0x5294, 0x0000,   /* pal 4: WHITE */
    0x0000, 0x2D08, 0x1084, 0x7FFF,   /* pal 5: DARK  */
    0x0000, 0x7FFF, 0x369F, 0x001F,   /* pal 6: RED   */
    0x0000, 0x5294, 0x39CE, 0x0000    /* pal 7: GRAY  */
};

static u16 entry(u8 glyph, u8 color)
{
    /* 2bpp map entry: priority set, palette 4+color */
    return (u16)(glyph | ((u16)(4 + (color & 3)) << 10) | 0x2000);
}

void ck_text_init(void)
{
    u16 g, r;
    const u8 *src = ck_font_chr;

    /* Upload glyphs with the ink transform: pixels that decoded to 2bpp
     * color 0 (EGA black letterforms) become opaque color 3; true
     * transparency exists only in the reserved filler glyph 0. Each 2bpp
     * row is a (plane0, plane1) byte pair. Direct VRAM writes under
     * forced blank (init paths hold the screen off). */
    REG_VMAIN = 0x80;
    REG_VMADDLH = CK_BG3_CHR_VRAM;
    for (g = 0; g < 256; g++) {
        for (r = 0; r < 8; r++) {
            u8 b0 = src[(u16)(g << 4) + (r << 1)];
            u8 b1 = src[(u16)(g << 4) + (r << 1) + 1];
            u8 e;
            if (g == 0) {
                REG_VMDATALH = 0;
                continue;
            }
            e = (u8)~(b0 | b1);
            REG_VMDATALH = (u16)((u8)(b0 | e) | ((u16)(u8)(b1 | e) << 8));
        }
    }

    dmaCopyCGram((u8 *)s_textPals, 16, 32);

    bgSetGfxPtr(2, CK_BG3_CHR_VRAM);
    bgSetMapPtr(2, CK_BG3_MAP_VRAM, SC_32x32);
    /* SNES BGs show line VOFS+1 on the top scanline: latch -1 so the
     * text grid truly starts at screen y 0 and lines up with BG1 (whose
     * camera latch already subtracts 1, render.c). The BG1 icon overlays
     * in the status box / ship dialog rely on this exact alignment. */
    bgSetScroll(2, 0, (u16)(-1) & 255);

    ck_text_clear();
    bgSetDisable(2);
}

void ck_text_clear(void)
{
    u16 i;
    for (i = 0; i < CK_TEXT_COLS * CK_TEXT_ROWS; i++)
        s_shadow[i] = 0;
    s_dirty = 1;
}

void ck_text_char(u8 x, u8 y, u8 glyph, u8 color)
{
    if (x >= CK_TEXT_COLS)
        return;
    if (y >= CK_TEXT_ROWS)
        return;
    s_shadow[((u16)y << 5) + x] = entry(glyph, color);
    s_dirty = 1;
}

void ck_text_string(u8 x, u8 y, const char *s, u8 color)
{
    while (*s) {
        ck_text_char(x, y, (u8)*s, color);
        s++;
        x++;
    }
}

void ck_text_box(u8 x, u8 y, u8 w, u8 h, u8 color)
{
    u8 i, j;
    if (w < 2)
        return;
    if (h < 2)
        return;

    ck_text_char(x, y, GLYPH_TL, color);
    ck_text_char((u8)(x + w - 1), y, GLYPH_TR, color);
    ck_text_char(x, (u8)(y + h - 1), GLYPH_BL, color);
    ck_text_char((u8)(x + w - 1), (u8)(y + h - 1), GLYPH_BR, color);
    for (i = 1; i < w - 1; i++) {
        ck_text_char((u8)(x + i), y, GLYPH_T, color);
        ck_text_char((u8)(x + i), (u8)(y + h - 1), GLYPH_B, color);
    }
    for (j = 1; j < h - 1; j++) {
        ck_text_char(x, (u8)(y + j), GLYPH_L, color);
        ck_text_char((u8)(x + w - 1), (u8)(y + j), GLYPH_R, color);
        for (i = 1; i < w - 1; i++)
            ck_text_char((u8)(x + i), (u8)(y + j), 0x20, color);
    }
}

void ck_text_show(u8 on)
{
    if (on)
        bgSetEnable(2);
    else
        bgSetDisable(2);
}

void ck_text_vblank(void)
{
    if (!s_dirty)
        return;
    /* The 1.8 KB map DMA takes ~5k cycles: only run it while the live
     * vblank flag is still set, else VRAM writes get dropped mid-copy
     * (PPU ignores them outside vblank). Retry next frame otherwise. */
    if (!(REG_HVBJOY & 0x80))
        return;
    dmaCopyVram((u8 *)s_shadow, CK_BG3_MAP_VRAM,
                CK_TEXT_COLS * CK_TEXT_ROWS * 2);
    s_dirty = 0;
}
