/*
 * gfx_gba.c — GBA-native replacement for src/render/gfx.c.
 *
 * Only the symbols the rest of the engine actually calls from outside
 * this TU are re-exported. Functions unique to the SDL/GL pipeline
 * (privCreate*, privDestroy*, privPrepareGLShaderProgram, etc.) are
 * dropped because nothing else in the tree references them.
 *
 * Rendering pipeline:
 *   engine_screen.client.byteEgaMemory holds a 384-pixel-wide
 *   palette-indexed buffer. We treat it as the "virtual framebuffer"
 *   exactly as the DOS code does. On CVort_engine_updateActualDisplay
 *   we blit the 320x200 visible portion into Mode 4's 240x160 VRAM,
 *   cropping to the centre (so overscan is lost but the action is
 *   preserved). The 16-entry EGA palette maps directly onto the low
 *   16 slots of the GBA background palette.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "core/globals.h"
#include "engine/engine_timing.h"
#include "episodes/episode1.h"
#include "platform/gba_data.h"
#include "third_party/cgenius/fileio/lz.h"

#include <gba_base.h>
#include <gba_video.h>
#include <gba_systemcalls.h>

/* --------------------------------------------------------------
 * File-local effective arguments mirror (shape irrelevant as long
 * as unused; kept so diagnostic lines with the same name compile).
 * -------------------------------------------------------------- */
struct {
    int fullWidth, fullHeight, windowWidth, windowHeight;
    gfxOutputSystem_T outputSystem;
    bool isFullscreen;
    int rendererDriverIndex;
    gfxScaleType_T scaleType;
    int zoomLevel;
    bool vSync;
    bool bilinearInterpolation;
    bool offScreenRendering;
} engine_gfx_effective_arguments;

/* --------------------------------------------------------------
 * Function-pointer exports. gfx.c chose these at video-mode switch
 * time; on the GBA we permanently bind them to no-op implementations
 * because pixels flow through CVort_engine_updateActualDisplay only.
 * -------------------------------------------------------------- */
static void gba_updateEgaGfxNonPalRect(uint32_t offset, uint16_t width, uint16_t height) {
    (void)offset; (void)width; (void)height;
}
static void gba_updateEgaNonBlinkingTxtNonPalRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    (void)x; (void)y; (void)w; (void)h;
}
static void gba_updateEgaBlinkingTxtNonPalChar(uint16_t x, uint16_t y, bool shown) {
    (void)x; (void)y; (void)shown;
}
static void gba_doDrawEgaTxtCursor(void) {}
static void gba_updateBorderedZoomedRectBuffer(uint8_t *buffer, uint32_t egaOffset, uint32_t borderLineOffset) {
    (void)buffer; (void)egaOffset; (void)borderLineOffset;
}
static void gba_updateBorderedZoomedRectBufferBorder(uint8_t *buffer) { (void)buffer; }
static void gba_gui_drawFontChar(int x, int y, int w, int h, const uint8_t *fontCharPtr, int colorNum, int ratio) {
    (void)x; (void)y; (void)w; (void)h; (void)fontCharPtr; (void)colorNum; (void)ratio;
}
static void gba_gui_drawRoundedRectBorder(int x, int y, int w, int h, int borderColorNum, int innerColorNum, int ratio) {
    (void)x; (void)y; (void)w; (void)h; (void)borderColorNum; (void)innerColorNum; (void)ratio;
}

void (*CVort_engine_updateEgaGfxNonPalRect_ptr)(uint32_t, uint16_t, uint16_t)           = gba_updateEgaGfxNonPalRect;
void (*CVort_engine_updateEgaNonBlinkingTxtNonPalRect_ptr)(uint16_t, uint16_t, uint16_t, uint16_t) = gba_updateEgaNonBlinkingTxtNonPalRect;
void (*CVort_engine_updateEgaBlinkingTxtNonPalChar_ptr)(uint16_t, uint16_t, bool)        = gba_updateEgaBlinkingTxtNonPalChar;
void (*CVort_engine_doDrawEgaTxtCursor_ptr)(void)                                        = gba_doDrawEgaTxtCursor;
void (*CVort_engine_updateBorderedZoomedRectBuffer_ptr)(uint8_t *, uint32_t, uint32_t)   = gba_updateBorderedZoomedRectBuffer;
void (*CVort_engine_updateBorderedZoomedRectBufferBorder_ptr)(uint8_t *)                 = gba_updateBorderedZoomedRectBufferBorder;
void (*CVort_engine_gui_drawFontChar_ptr)(int, int, int, int, const uint8_t *, int, int) = gba_gui_drawFontChar;
void (*CVort_engine_gui_drawRoundedRectBorder_ptr)(int, int, int, int, int, int, int)    = gba_gui_drawRoundedRectBorder;

/* --------------------------------------------------------------
 * Source-coordinate window. The virtual buffer is 384 pixels wide
 * (ENGINE_EGA_GFX_SCANLINE_LEN). The visible game area is 320x200.
 *
 * We fill the full 240x160 LCD with a non-uniform nearest-neighbour
 * downscale (3/4 horizontal, 4/5 vertical). Everything is visible,
 * no letterbox bars, and the aspect ratio is squished by ~6%
 * (320/200 = 1.60 source vs 240/160 = 1.50 LCD) — negligible for
 * game art. Source columns/rows dropped by the downscale are
 * max-merged into their kept neighbour so 1-pixel font strokes on
 * the dropped lanes don't vanish. */
#define GBA_LCD_WIDTH       240
#define GBA_LCD_HEIGHT      160
#define SRC_VISIBLE_WIDTH   ENGINE_EGA_GFX_WIDTH     /* 320 */
#define SRC_VISIBLE_HEIGHT  ENGINE_EGA_GFX_HEIGHT    /* 200 */

#if SRC_VISIBLE_WIDTH != 320 || SRC_VISIBLE_HEIGHT != 200 \
    || (ENGINE_EGA_GFX_SCANLINE_LEN & 3)
#error "gba_blitScaledFrame hard-codes a 320x200 source with word-aligned stride"
#endif

/* The blit reads byteEgaMemory as 32-bit words; both page starts
 * (0 and 0x3000) and the stride (384) are word multiples, so only the
 * array's own offset inside engine_screen needs checking. */
_Static_assert((offsetof(CVort_engine_screen_T, client.byteEgaMemory) & 3) == 0,
               "byteEgaMemory must be word-aligned for the GBA blit");

static bool g_gbaModeInitialised = false;

/* Hot per-frame blit, specialised for the fixed 320x200 -> 240x160
 * downscale. Runs as ARM code from IWRAM: instruction fetches are 0-wait
 * 32-bit instead of ROM's wait-stated 16-bit ones.
 *
 * Horizontally, every 4 source pixels produce 3 destination pixels
 * (source px 3 max-merges into dest px 2). Reading the source as two
 * aligned words per 8 pixels halves the EWRAM bus traffic versus
 * per-byte loads (one 32-bit EWRAM read costs 6 cycles vs 4x3 for
 * bytes). Vertically, every 5 source rows produce 4 destination rows;
 * the dropped row (phase y%4 == 3) max-merges into the row above it. */
IWRAM_CODE ARM_CODE __attribute__((noinline))
static void gba_blitScaledFrame(const uint8_t *srcBase, uint16_t *dst) {
    int sy = 0;
    for (int y = 0; y < GBA_LCD_HEIGHT; y++) {
        const uint32_t *s0 = (const uint32_t *)(srcBase
                           + sy * ENGINE_EGA_GFX_SCANLINE_LEN);
        if ((y & 3) != 3) {
            /* Fast path — this dest row samples a single source row. */
            for (int g = 0; g < GBA_LCD_WIDTH / 6; g++) {
                uint32_t w0 = *s0++;
                uint32_t w1 = *s0++;
                uint32_t p0 =  w0        & 0xFF;
                uint32_t p1 = (w0 >> 8)  & 0xFF;
                uint32_t p2 = (w0 >> 16) & 0xFF;
                uint32_t p3 =  w0 >> 24;
                if (p3 > p2) p2 = p3;
                uint32_t p4 =  w1        & 0xFF;
                uint32_t p5 = (w1 >> 8)  & 0xFF;
                uint32_t p6 = (w1 >> 16) & 0xFF;
                uint32_t p7 =  w1 >> 24;
                if (p7 > p6) p6 = p7;
                *dst++ = (uint16_t)(p0 | (p1 << 8));
                *dst++ = (uint16_t)(p2 | (p4 << 8));
                *dst++ = (uint16_t)(p5 | (p6 << 8));
            }
            sy += 1;
        } else {
            /* Merge path — max-merge the dropped source row (sy+1). */
            const uint32_t *s1 = (const uint32_t *)(srcBase
                               + (sy + 1) * ENGINE_EGA_GFX_SCANLINE_LEN);
            for (int g = 0; g < GBA_LCD_WIDTH / 6; g++) {
                uint32_t w0 = *s0++;
                uint32_t w1 = *s0++;
                uint32_t v0 = *s1++;
                uint32_t v1 = *s1++;
                uint32_t p, q;
#define GBA_LANE_MAX(dstv, wa, va, shift)                     \
                p = ((wa) >> (shift)) & 0xFF;                 \
                q = ((va) >> (shift)) & 0xFF;                 \
                dstv = (q > p) ? q : p;
                uint32_t p0, p1, p2, p3, p4, p5, p6, p7;
                GBA_LANE_MAX(p0, w0, v0, 0)
                GBA_LANE_MAX(p1, w0, v0, 8)
                GBA_LANE_MAX(p2, w0, v0, 16)
                GBA_LANE_MAX(p3, w0, v0, 24)
                if (p3 > p2) p2 = p3;
                GBA_LANE_MAX(p4, w1, v1, 0)
                GBA_LANE_MAX(p5, w1, v1, 8)
                GBA_LANE_MAX(p6, w1, v1, 16)
                GBA_LANE_MAX(p7, w1, v1, 24)
                if (p7 > p6) p6 = p7;
#undef GBA_LANE_MAX
                *dst++ = (uint16_t)(p0 | (p1 << 8));
                *dst++ = (uint16_t)(p2 | (p4 << 8));
                *dst++ = (uint16_t)(p5 | (p6 << 8));
            }
            sy += 2;
        }
    }
}

static inline uint16_t rgb888_to_bgr555(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >>  8) & 0xFF;
    uint8_t b = (rgb      ) & 0xFF;
    return (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
}

static void gba_pushDefaultPalette(void) {
    for (int i = 0; i < 16; i++) {
        BG_COLORS[i] = rgb888_to_bgr555(engine_egaRGBColorTable[i]);
    }
}

static void gba_ensureMode4(void) {
    if (g_gbaModeInitialised) return;
    /* Mode 4: 240x160 256-colour bitmap, BG2 holds the framebuffer. */
    REG_DISPCNT = MODE_4 | BG2_ENABLE;
    gba_pushDefaultPalette();
    /* Clear the active front buffer to colour 0. */
    {
        /* Clear the framebuffer. A simple halfword loop is fine here —
         * this only runs on mode switch, not per-frame. */
        uint16_t *fb = (uint16_t *)VRAM;
        for (int i = 0; i < (GBA_LCD_WIDTH * GBA_LCD_HEIGHT) / 2; i++) fb[i] = 0;
    }
    g_gbaModeInitialised = true;
}

/* --------------------------------------------------------------
 * Public drawing entry points. These are called by the game loop
 * and episodes; all heavy work funnels into CVort_engine_doDrawing.
 * -------------------------------------------------------------- */
/* Lazy-tile state: Keen 1's 4-plane EGALATCH decompresses to ~120 KiB,
 * which is bigger than the entire GBA heap. scripts/bake_gba_data.sh
 * pre-decompresses EGALATCH on the build host and the FILE* shim hands
 * back its ROM pointer directly, so at runtime g_gbaLatchData points
 * into cart ROM — no heap cost, read-only. We unpack each 16×16 tile
 * on demand; bmps and fonts are small enough to unpack once. */
static const uint8_t *g_gbaLatchData     = NULL;
static uint32_t       g_gbaLatchPlaneSize = 0;
static uint32_t       g_gbaTileLoc       = 0;
static uint16_t       g_gbaTileNum       = 0;

/* Tile decode cache. adaptiveTileRefresh redraws the full visible grid
 * (21x13 = 273 tiles) every frame; without a cache, each call decoded
 * 4 planes from wait-stated ROM via a 256-iteration bit-test loop —
 * that was the dominant per-frame cost (~80-100ms) and what made the
 * game feel like slow-motion.
 *
 * Direct-mapped by tile number. Slot count is a power-of-two so "% N"
 * folds to a bitmask. Collisions (two visible tiles sharing a slot)
 * degrade gracefully: the cache thrashes for that pair, but the common
 * case is "every slot stable for the lifetime of the level". For Keen 1
 * (611 total tiles, usually <200 on screen at once) 256 slots is ample.
 *
 * Layout is 1 tag byte + 256 pixel bytes = 257 per slot; round to 264
 * for alignment. Total ~66 KiB in EWRAM (.sbss) — fits comfortably in
 * the ~90 KiB EWRAM headroom. */
#define GBA_TILE_CACHE_SLOTS 256
#define GBA_TILE_CACHE_MASK  (GBA_TILE_CACHE_SLOTS - 1)
#define GBA_TILE_EMPTY       0xFFFF
static uint8_t  s_gbaTileCache[GBA_TILE_CACHE_SLOTS][256]
    __attribute__((section(".sbss"), aligned(4)));
static uint16_t s_gbaTileCacheOwner[GBA_TILE_CACHE_SLOTS]
    __attribute__((section(".sbss")));
static bool     s_gbaTileCacheInited
    __attribute__((section(".sbss")));

static void gba_tileCacheInit(void) {
    if (s_gbaTileCacheInited) return;
    for (int i = 0; i < GBA_TILE_CACHE_SLOTS; i++) {
        s_gbaTileCacheOwner[i] = GBA_TILE_EMPTY;
    }
    s_gbaTileCacheInited = true;
}

static const uint8_t *gba_decodeTile(uint16_t num) {
    if (!g_gbaLatchData || num >= g_gbaTileNum) return NULL;
    gba_tileCacheInit();
    const uint32_t slot = num & GBA_TILE_CACHE_MASK;
    uint8_t *dst = s_gbaTileCache[slot];
    if (s_gbaTileCacheOwner[slot] == num) return dst;

    /* Cache miss — decode from the 4-plane latch. Read each plane byte
     * ONCE and fan it out to 8 pixels (prior code re-read the same byte
     * 8 times, once per bit, which burned 7 redundant wait-stated ROM
     * reads per pixel). */
    const uint8_t *planeBase = g_gbaLatchData + g_gbaTileLoc + (uint32_t)num * 32;
    const uint8_t *p0 = planeBase;
    const uint8_t *p1 = planeBase + g_gbaLatchPlaneSize;
    const uint8_t *p2 = planeBase + 2 * g_gbaLatchPlaneSize;
    const uint8_t *p3 = planeBase + 3 * g_gbaLatchPlaneSize;
    for (int byteIdx = 0; byteIdx < 32; byteIdx++) {
        uint8_t b0 = p0[byteIdx];
        uint8_t b1 = p1[byteIdx];
        uint8_t b2 = p2[byteIdx];
        uint8_t b3 = p3[byteIdx];
        uint8_t *row = dst + byteIdx * 8;
        for (int bit = 7; bit >= 0; bit--) {
            row[7 - bit] = (uint8_t)(((b0 >> bit) & 1)
                                   | (((b1 >> bit) & 1) << 1)
                                   | (((b2 >> bit) & 1) << 2)
                                   | (((b3 >> bit) & 1) << 3));
        }
    }
    s_gbaTileCacheOwner[slot] = num;
    return dst;
}

/* --------------------------------------------------------------
 * Adaptive-tile-refresh dirty-cell cache.
 *
 * adaptiveTileRefresh used to redraw the full 21x13 visible grid every
 * frame — 273 tile blits (~70 KiB of EWRAM writes) even when nothing
 * moved. Instead we remember, per framebuffer page and grid cell, which
 * tile was last blitted there and skip cells that still hold it. The
 * cache is keyed to the tile-aligned scroll origin: when the origin
 * shifts (every 16 px of scroll) the page repaints in full, but static
 * frames — most of gameplay, menus, the worldmap — reduce to blitting
 * only animated tiles.
 *
 * Anything else that writes into byteEgaMemory (sprites, chars, bmps,
 * draw_func overlays, full clears) must invalidate the cells it touches
 * so the next refresh restores the map beneath it; see gba_atrMarkRect
 * callers below.
 * -------------------------------------------------------------- */
#define GBA_ATR_COLS 21
#define GBA_ATR_ROWS 13
#define GBA_ATR_NONE 0xFFFF
static uint16_t s_gbaAtrOwner[2][GBA_ATR_ROWS * GBA_ATR_COLS]
    __attribute__((section(".sbss")));
static int32_t  s_gbaAtrScrollX[2], s_gbaAtrScrollY[2];
static bool     s_gbaAtrValid[2];

static inline int gba_atrPage(void) { return engine_dstPage ? 1 : 0; }

static void gba_atrInvalidateAll(void) {
    s_gbaAtrValid[0] = false;
    s_gbaAtrValid[1] = false;
}

/* Mark the grid cells covering pixel rect [px,px+w)x[py,py+h) of the
 * current destination page as needing a repaint on the next refresh. */
static void gba_atrMarkRect(int px, int py, int w, int h) {
    const int page = gba_atrPage();
    if (!s_gbaAtrValid[page]) return;
    int tx0 = px >> 4;
    int ty0 = py >> 4;
    int tx1 = (px + w - 1) >> 4;
    int ty1 = (py + h - 1) >> 4;
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 >= GBA_ATR_COLS) tx1 = GBA_ATR_COLS - 1;
    if (ty1 >= GBA_ATR_ROWS) ty1 = GBA_ATR_ROWS - 1;
    uint16_t *owner = s_gbaAtrOwner[page];
    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            owner[ty * GBA_ATR_COLS + tx] = GBA_ATR_NONE;
        }
    }
}

/* 16-px-wide tile row blit as 4 word copies per row. Both sides are
 * word-aligned: the tile cache slots are aligned(4), and dst offsets are
 * page (0/0x3000) + py*384 + a multiple of 8. Word accesses halve the
 * EWRAM bus traffic versus byte copies and skip memcpy call overhead. */
IWRAM_CODE ARM_CODE __attribute__((noinline))
static void gba_blitTile16(uint8_t *dst, const uint8_t *src, int h) {
    for (int y = 0; y < h; y++) {
        uint32_t *d = (uint32_t *)dst;
        const uint32_t *s = (const uint32_t *)src;
        uint32_t a = s[0], b = s[1], c = s[2], e = s[3];
        d[0] = a; d[1] = b; d[2] = c; d[3] = e;
        dst += ENGINE_EGA_GFX_SCANLINE_LEN;
        src += 16;
    }
}

/* Backing storage for the sprite/font pointer tables. The runtime fills
 * these in once from the ROM-mapped pools, so every engine_egaSprites[]
 * / engine_egaFonts[] deref is just a pointer table lookup — no heap.
 *
 * Sized for Keen 1 (spriteNum = 119, fontNum = 256); episodes 2/3 aren't
 * built for the GBA. If a future episode exceeds these, the binds below
 * early-return without corrupting anything. */
#define GBA_MAX_SPRITE_ENTRIES 256
#define GBA_MAX_FONT_ENTRIES   256
static uint8_t *s_gbaEgaSpritesBacking[GBA_MAX_SPRITE_ENTRIES]
    __attribute__((section(".sbss")));
static uint8_t *s_gbaEgaFontsBacking[GBA_MAX_FONT_ENTRIES]
    __attribute__((section(".sbss")));

/* Zero-malloc graphics init. Everything heavy is pre-decompressed and
 * pre-unpacked by scripts/gba_decomp_graphics_host.c; here we just fread
 * the 48-byte general header (engine_egaHeadGeneral) and bind a handful
 * of runtime pointers to ROM entries:
 *
 *   EGAHEAD.<ext>  — general section only (first 48 bytes).
 *   EGABMPHD.<ext> — engine_egaHeadUnmasked[] struct array
 *   EGASPRHD.<ext> — engine_maskedSpriteEntry[] struct array
 *   EGALATCH.<ext> — raw plane-packed latch (for lazy tile/bmp decode)
 *   EGASPRUN.<ext> — unpacked 8bpp sprite pool
 *   EGAFNTUN.<ext> — unpacked 8bpp font pool
 *
 * Tiles are decoded on demand by gba_decodeTile; bmps by the override in
 * CVort_engine_drawBitmap — both walk the plane-packed latch directly. */
void CVort_engine_decompGraphics(void) {
    snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGAHEAD.%s", game_ext);
    FILE *fp = CVort_engine_cross_ro_data_fopen(g_game.string_buf);
    if (!fp) return;
#define CHOCOLATE_KEEN_READ16_OR_RETURN(dst) \
    do { \
        if (CVort_engine_cross_freadInt16LE((dst), 1, fp) != 1) { fclose(fp); return; } \
    } while (0)
#define CHOCOLATE_KEEN_READ32_OR_RETURN(dst) \
    do { \
        if (CVort_engine_cross_freadInt32LE((dst), 1, fp) != 1) { fclose(fp); return; } \
    } while (0)
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.latchPlaneSize);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.sprPlaneSize);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.imgDataStart);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.sprDataStart);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.fontNum);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.fontLoc);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.unkNum);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.unkLoc);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.tileNum);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.tileLoc);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.bmpNum);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.bmpLoc);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.spriteNum);
    CHOCOLATE_KEEN_READ32_OR_RETURN(&engine_egaHeadGeneral.spriteLoc);
    CHOCOLATE_KEEN_READ16_OR_RETURN(&engine_egaHeadGeneral.compression);
    fclose(fp);
#undef CHOCOLATE_KEEN_READ32_OR_RETURN
#undef CHOCOLATE_KEEN_READ16_OR_RETURN

    /* Step 1: bind metadata struct arrays from ROM. The on-disk layout
     * of EGABMPHD / EGASPRHD (baked by gba_decomp_graphics_host) matches
     * the in-memory struct layout, so a cast is all we need. */
    {
        const uint8_t *rom = NULL; size_t sz = 0;
        snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGABMPHD.%s", game_ext);
        if (ck_gba_lookup_rom(g_game.string_buf, &rom, &sz) != 0) return;
        engine_egaHeadUnmasked = (EGAHeadUnmasked_T *)(void *)rom;

        snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGASPRHD.%s", game_ext);
        if (ck_gba_lookup_rom(g_game.string_buf, &rom, &sz) != 0) return;
        engine_maskedSpriteEntry = (MaskedSpriteEntry_T *)(void *)rom;
    }

    /* Step 2: bind unpacked sprite pool. Each base sprite's offset into
     * the pool is computed linearly from the metadata. */
    {
        const uint8_t *rom = NULL; size_t sz = 0;
        snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGASPRUN.%s", game_ext);
        if (ck_gba_lookup_rom(g_game.string_buf, &rom, &sz) != 0) return;
        engine_egaSpriteData = (uint8_t *)(void *)rom;
        if (engine_egaHeadGeneral.spriteNum <= GBA_MAX_SPRITE_ENTRIES) {
            engine_egaSprites = s_gbaEgaSpritesBacking;
            uint32_t acc = 0;
            for (uint16_t i = 0; i < engine_egaHeadGeneral.spriteNum; i++) {
                s_gbaEgaSpritesBacking[i] = engine_egaSpriteData + acc;
                /* Every 4th entry is a base sprite; the other three shifts
                 * reuse its unpacked pixels. */
                acc += 8u * engine_maskedSpriteEntry[i * 4].width
                          * engine_maskedSpriteEntry[i * 4].height;
            }
        } else {
            engine_egaSprites = NULL;
        }
    }

    /* Step 3: bind EGALATCH for on-demand tile/bmp decode. */
    {
        const uint8_t *rom = NULL; size_t sz = 0;
        snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGALATCH.%s", game_ext);
        if (ck_gba_lookup_rom(g_game.string_buf, &rom, &sz) != 0) return;
        g_gbaLatchData      = rom;
        g_gbaLatchPlaneSize = engine_egaHeadGeneral.latchPlaneSize;
        g_gbaTileLoc        = engine_egaHeadGeneral.tileLoc;
        g_gbaTileNum        = engine_egaHeadGeneral.tileNum;
    }

    CVort_engine_setVideoMode(CVORT_VIDEO_MODE_GRAPHICS);

    /* Step 4: bmps stay lazy (decoded from the ROM latch on demand by
     * CVort_engine_drawBitmap). The TITLE mural alone is ~29 KiB, so
     * unpacking the full 18-bmp pool eagerly would blow the heap. */
    engine_egaBmpData = NULL;
    engine_egaBmps   = NULL;

    /* Step 5: bind unpacked font pool and fill the pointer table. */
    {
        const uint8_t *rom = NULL; size_t sz = 0;
        snprintf(g_game.string_buf, sizeof(g_game.string_buf), "EGAFNTUN.%s", game_ext);
        if (ck_gba_lookup_rom(g_game.string_buf, &rom, &sz) != 0) return;
        engine_egaFontData = (uint8_t *)(void *)rom;
        if (engine_egaHeadGeneral.fontNum <= GBA_MAX_FONT_ENTRIES) {
            engine_egaFonts = s_gbaEgaFontsBacking;
            for (uint16_t i = 0; i < engine_egaHeadGeneral.fontNum; i++) {
                s_gbaEgaFontsBacking[i] = engine_egaFontData + 64u * i;
            }
        } else {
            engine_egaFonts = NULL;
        }
    }

    CVort_engine_setBorderColor(3);
    if (engine_gameVersion == GAMEVER_KEEN1) {
        CVort_engine_drawBitmap(0xf, 0x4c, CVort1_bmp_onemomen);
    }
}

void CVort_engine_gui_clearScreen(void) {
    /* Mirror the SDL path (see gfx.c:727): wipe the virtual framebuffer,
     * not just VRAM. Without this, transitions from title → worldmap →
     * high scores → game over composite on top of each other because the
     * next frame's blit sources stale bytes left over from the previous
     * scene. */
    memset(engine_screen.client.byteEgaMemory, 0,
           sizeof(engine_screen.client.byteEgaMemory));
    gba_atrInvalidateAll();
    engine_isFrameReadyToDisplay = true;

    if (g_gbaModeInitialised) {
        uint16_t *fb = (uint16_t *)VRAM;
        for (int i = 0; i < (GBA_LCD_WIDTH * GBA_LCD_HEIGHT) / 2; i++) fb[i] = 0;
    }
}

void CVort_engine_gui_drawColoredLine(int lineNum, int lineLength, int colorNum) {
    (void)lineNum; (void)lineLength; (void)colorNum;
}

void CVort_engine_gui_drawColoredColumn(int columnNum, int columnLength, int colorNum) {
    (void)columnNum; (void)columnLength; (void)colorNum;
}

void CVort_engine_updateActualDisplay(void) {
    gba_ensureMode4();

    if (!engine_isFrameReadyToDisplay) return;

    const uint8_t *srcBase = engine_screen.client.byteEgaMemory
                           + engine_currPageStart;
    uint16_t *dst = (uint16_t *)VRAM;

    /* Non-uniform NN downscale 320x200 → 240x160, filling the LCD.
     * Heavy lifting runs from IWRAM (gba_blitScaledFrame) so the per-
     * frame blit isn't bottlenecked on ROM instruction fetches. */
    gba_blitScaledFrame(srcBase, dst);

    engine_isFrameReadyToDisplay = false;
    engine_lastDisplayUpdateTime = SDL_GetTicks();
}

void CVort_engine_setWindowTitleAndIcon(void) {}

void CVort_engine_reactToWindowResize(int width, int height) { (void)width; (void)height; }

#if SDL_VERSION_ATLEAST(2,0,0)
void CVort_engine_handleWindowSideChange(void) {}
#endif

bool CVort_engine_prepareScreen(void) {
    /* Fill engine_screen with defaults the rest of the engine reads. */
    engine_screen.host.bytesPerPixel = 1;
    engine_screen.host.isIndexedColorFormatted = true;
    engine_screen.host.egaMemoryPtr = engine_screen.client.byteEgaMemory;
    engine_screen.host.colorTable = NULL;
    engine_screen.host.mappedEgaColorTable = NULL;
    engine_screen.host.desktopWidth = GBA_LCD_WIDTH;
    engine_screen.host.desktopHeight = GBA_LCD_HEIGHT;
    engine_screen.host.fullWidth = GBA_LCD_WIDTH;
    engine_screen.host.fullHeight = GBA_LCD_HEIGHT;
    engine_screen.host.winWidth = GBA_LCD_WIDTH;
    engine_screen.host.winHeight = GBA_LCD_HEIGHT;
    engine_screen.host.useFullDesktopDims = true;

    engine_screen.dims.clientScanLineLength = ENGINE_EGA_GFX_SCANLINE_LEN;
    engine_screen.dims.clientRect.x = 0;
    engine_screen.dims.clientRect.y = 0;
    engine_screen.dims.clientRect.w = SRC_VISIBLE_WIDTH;
    engine_screen.dims.clientRect.h = SRC_VISIBLE_HEIGHT;

    engine_screen.client.currVidMode = CVORT_VIDEO_MODE_GRAPHICS;
    engine_screen.client.currEgaStartAddr = 0;
    engine_screen.client.currPanning = 0;
    engine_screen.client.totalScanHeight = ENGINE_VGA_TOTAL_SCANLINE_COUNT;
    engine_screen.client.vertRetraceLen = ENGINE_VGA_VERTICAL_RETRACE_LEN;

    gba_ensureMode4();
    return true;
}

bool CVort_engine_resetWindow(void) {
    return CVort_engine_prepareScreen();
}

bool CVort_engine_setVideoMode(int16_t vidMode) {
    /* Mirror privSetVideoModeLow (gfx.c:2074): switching into graphics
     * mode zeroes byteEgaMemory. The game relies on this when moving
     * between scenes (title → worldmap → game over → high scores);
     * without it, stale pixels from the prior scene bleed through. */
    if (vidMode == CVORT_VIDEO_MODE_GRAPHICS) {
        memset(engine_screen.client.byteEgaMemory, 0,
               sizeof(engine_screen.client.byteEgaMemory));
        gba_atrInvalidateAll();
    }
    engine_screen.client.currVidMode = vidMode;
    engine_isFrameReadyToDisplay = true;
    return true;
}

bool CVort_engine_isVerticalBlank(void) {
    /* DISPSTAT bit 0 = vblank. */
    return (REG_DISPSTAT & 1) != 0;
}

void CVort_engine_copyToTxtMemory(uint8_t *buffer) {
    memcpy(engine_screen.client.egaTxtMemory, buffer,
           sizeof(engine_screen.client.egaTxtMemory));
}

/* --------------------------------------------------------------
 * Tile / sprite / char drawers. The engine walks queued-draw
 * arrays and pushes into byteEgaMemory. We port the minimal
 * inner-loop so the virtual framebuffer reflects gameplay state.
 * -------------------------------------------------------------- */
static inline void gba_blitIndexed8(uint8_t *dst, const uint8_t *src,
                                    int w, int h, int dstStride, int srcStride) {
    for (int y = 0; y < h; y++) {
        memcpy(dst, src, w);
        dst += dstStride;
        src += srcStride;
    }
}

void CVort_engine_drawChar(uint16_t x, uint16_t y, uint16_t val) {
    /* Port of the SDL path: fonts are pre-decoded 8x8x1-byte glyphs in
     * EGAFNTUN, bound to engine_egaFonts at graphics-decomp time. Copy
     * one glyph into byteEgaMemory; the usual 384-wide stride applies. */
    if (!engine_egaFonts || val >= engine_egaHeadGeneral.fontNum) return;
    const uint8_t *fontPixelPtr = engine_egaFonts[val];
    if (!fontPixelPtr) return;
    int px = (int)x * 8;
    int py = (int)y;
    if (px < 0 || py < 0) return;
    if (px + 8 > ENGINE_EGA_GFX_SCANLINE_LEN) return;
    if (py + 8 > SRC_VISIBLE_HEIGHT) return;
    uint8_t *dst = engine_screen.client.byteEgaMemory
                 + engine_dstPage
                 + py * ENGINE_EGA_GFX_SCANLINE_LEN + px;
    for (int cy = 0; cy < 8; cy++) {
        memcpy(dst, fontPixelPtr, 8);
        dst += ENGINE_EGA_GFX_SCANLINE_LEN;
        fontPixelPtr += 8;
    }
    gba_atrMarkRect(px, py, 8, 8);
    engine_isFrameReadyToDisplay = true;
}

void CVort_engine_drawSprite(uint16_t x, uint16_t y, uint16_t num) {
    /* Matches the SDL convention (gfx.c:2950+): `num` is a sprite_copy
     * (frame<<2 | shift), where the low 2 bits encode a 0/2/4/6-pixel
     * right-shift applied on top of the byte-column `x`. engine_egaSprites
     * is indexed by frame, and engine_maskedSpriteEntry[frame*4] supplies
     * the dimensions. EGASPRUN stores 5-bit pixels (bit 4 = mask, bits 0-3
     * = color). Uses the SDL blit equation verbatim so that exotic data
     * (mask=1 with non-zero color) blends the same as the PC version. */
    if (!engine_egaSprites || !engine_maskedSpriteEntry) return;
    if (num >= 4u * engine_egaHeadGeneral.spriteNum) num = 0;
    uint16_t frame = num >> 2;
    if (!engine_egaSprites[frame]) return;
    const MaskedSpriteEntry_T *ent = &engine_maskedSpriteEntry[num & ~3u];
    int subShift = (int)(num & 3) * 2;
    int px = (int)x * 8 + subShift;
    int py = (int)y;
    int w = ent->width * 8;
    int h = ent->height;
    if (px < 0 || py < 0) return;
    int copyW = w;
    if (px + copyW > ENGINE_EGA_GFX_SCANLINE_LEN) copyW = ENGINE_EGA_GFX_SCANLINE_LEN - px;
    if (py + h > SRC_VISIBLE_HEIGHT) h = SRC_VISIBLE_HEIGHT - py;
    if (copyW <= 0 || h <= 0) return;
    uint8_t *dstBase = engine_screen.client.byteEgaMemory
                     + engine_dstPage
                     + py * ENGINE_EGA_GFX_SCANLINE_LEN + px;
    const uint8_t *srcBase = engine_egaSprites[frame];
    for (int yy = 0; yy < h; yy++) {
        uint8_t *dst = dstBase + yy * ENGINE_EGA_GFX_SCANLINE_LEN;
        const uint8_t *src = srcBase + yy * w;
        for (int xx = 0; xx < copyW; xx++) {
            uint8_t v = src[xx];
            uint8_t maskFill = (uint8_t)(((v >> 4) & 1) * 0x0F);
            dst[xx] = (uint8_t)((maskFill & dst[xx]) | (v & 0x0F));
        }
    }
    gba_atrMarkRect(px, py, copyW, h);
}

/* Matches the SDL convention (gfx.c:2992): x is a byte-column (one
 * byte = 8 pixels, one tile = 2 bytes = 16 pixels), y is a raw pixel
 * row. Tiles overlapping the bottom edge are height-clipped. Returns
 * true if any pixels were written. */
static bool gba_drawTileCommon(uint16_t x, uint16_t y, uint16_t num) {
    const uint8_t *src = gba_decodeTile(num);
    if (!src) return false;
    int px = (int)x * 8;
    int py = (int)y;
    if (px < 0 || py < 0) return false;
    if (px + 16 > ENGINE_EGA_GFX_SCANLINE_LEN) return false;
    int h = 16;
    if (py + h > SRC_VISIBLE_HEIGHT) h = SRC_VISIBLE_HEIGHT - py;
    if (h <= 0) return false;
    uint8_t *dst = engine_screen.client.byteEgaMemory
                 + engine_dstPage
                 + py * ENGINE_EGA_GFX_SCANLINE_LEN + px;
    gba_blitTile16(dst, src, h);
    return true;
}

void CVort_engine_drawTile(uint16_t x, uint16_t y, uint16_t num) {
    if (gba_drawTileCommon(x, y, num)) {
        /* Queued tile draws (sprite-background restores) can land at
         * sub-cell offsets; the touched cells must repaint next frame. */
        gba_atrMarkRect((int)x * 8, (int)y, 16, 16);
    }
}

void CVort_engine_drawBitmap(uint16_t x, uint16_t y, uint16_t num) {
    /* Keen 1's bmps live in EGALATCH as 4-plane packed data; engine_egaBmps
     * is not allocated on GBA to save ~65 KiB of heap. Decode each byte's
     * 8 pixels into the virtual framebuffer by ANDing the four plane bits. */
    if (!g_gbaLatchData || !engine_egaHeadUnmasked) return;
    if (num >= engine_egaHeadGeneral.bmpNum) return;
    const EGAHeadUnmasked_T *hdr = &engine_egaHeadUnmasked[num];
    int px = (int)x * 8;
    int py = (int)y;
    int wBytes = hdr->h;
    int wPixels = wBytes * 8;
    int h = hdr->v;
    if (px < 0 || py < 0) return;
    if (wPixels <= 0 || h <= 0) return;
    int clipPixels = wPixels;
    if (px + clipPixels > ENGINE_EGA_GFX_SCANLINE_LEN)
        clipPixels = ENGINE_EGA_GFX_SCANLINE_LEN - px;
    if (py + h > SRC_VISIBLE_HEIGHT) h = SRC_VISIBLE_HEIGHT - py;
    if (clipPixels <= 0 || h <= 0) return;

    const uint8_t *p0 = g_gbaLatchData + engine_egaHeadGeneral.bmpLoc + hdr->loc;
    const uint8_t *p1 = p0 + g_gbaLatchPlaneSize;
    const uint8_t *p2 = p0 + 2 * g_gbaLatchPlaneSize;
    const uint8_t *p3 = p0 + 3 * g_gbaLatchPlaneSize;
    uint8_t *dstBase = engine_screen.client.byteEgaMemory
                     + engine_dstPage
                     + py * ENGINE_EGA_GFX_SCANLINE_LEN + px;
    for (int yy = 0; yy < h; yy++) {
        uint8_t *dstRow = dstBase + yy * ENGINE_EGA_GFX_SCANLINE_LEN;
        int rowBase = yy * wBytes;
        for (int bx = 0; bx < wBytes; bx++) {
            int dxBase = bx * 8;
            if (dxBase >= clipPixels) break;
            uint8_t b0 = p0[rowBase + bx];
            uint8_t b1 = p1[rowBase + bx];
            uint8_t b2 = p2[rowBase + bx];
            uint8_t b3 = p3[rowBase + bx];
            for (int k = 0; k < 8; k++) {
                int dx = dxBase + k;
                if (dx >= clipPixels) break;
                uint8_t mask = (uint8_t)(1 << (7 - k));
                uint8_t v = (uint8_t)(((b0 & mask) ? 1 : 0)
                                   | ((b1 & mask) ? 2 : 0)
                                   | ((b2 & mask) ? 4 : 0)
                                   | ((b3 & mask) ? 8 : 0));
                dstRow[dx] = v;
            }
        }
    }
    gba_atrMarkRect(px, py, clipPixels, h);
}

uint16_t CVort_engine_drawSpriteAt(int32_t pos_x, int32_t pos_y, uint16_t frame) {
    /* Port of the SDL enqueue path (gfx.c:3068). pos_x/pos_y are 8.8 fixed
     * point; scroll_x/scroll_y are engine scroll origins in the same units.
     * The subpixel delta, converted to pixels and measured from the
     * tile-aligned scroll boundary, becomes the on-screen position.
     *
     * On the previous direct-draw path we were passing the pixel offset as
     * drawSprite's byte-column, giving an 8x position error. On the
     * worldmap, keen_gp.screenX = mapX + 0xFFFF7000 makes scroll_x
     * negative, so the offset runs up to ~150 px and the sprite ended up
     * at px=1200 — far off screen, making Keen invisible every frame. */
    if (spritedraws_i >= 0x1f4) return 0;
    int16_t xbyte = (int16_t)((pos_x / 0x100) - ((scroll_x / 0x100) & 0xFFF0));
    int16_t yrow  = (int16_t)((pos_y / 0x100) - ((scroll_y / 0x100) & 0xFFF0));
    if (xbyte < -0x20 || yrow < -0x20 || xbyte > 0x150 || yrow > 0xC7)
        return 0;
    uint16_t scopy = (uint16_t)(((xbyte & 7) / 2) + (frame << 2));
    xbyte = (int16_t)((xbyte + 0x20) / 8 - 4);
    xbyte += 4;
    yrow  += 0x20;
    spritedraws[spritedraws_i].x_byte = xbyte;
    spritedraws[spritedraws_i].y_row = yrow;
    spritedraws[spritedraws_i].sprite_copy = scopy;
    spritedraws_c++;
    spritedraws_i++;
    return 1;
}

uint16_t CVort_engine_drawTileAt(int32_t pos_x, int32_t pos_y, uint16_t tilenum) {
    /* Port of the SDL enqueue path (gfx.c:3146). Same coordinate layout as
     * drawSpriteAt; the +4/+0x20 panning margin is baked in so the shared
     * doDrawing loop's -4/-0x20 back-out produces on-screen pixels. */
    if (tiledraws_i >= 0x64) return 0;
    int16_t xbyte = (int16_t)((pos_x / 0x100) - ((scroll_x / 0x100) & 0xFFF0));
    int16_t yrow  = (int16_t)((pos_y / 0x100) - ((scroll_y / 0x100) & 0xFFF0));
    if (xbyte < -0x20 || yrow < -0x20 || xbyte > 0x150 || yrow > 0xC7)
        return 0;
    xbyte = (int16_t)((xbyte + 0x20) / 8 - 4);
    xbyte += 4;
    yrow  += 0x20;
    tiledraws[tiledraws_i].x_byte = xbyte;
    tiledraws[tiledraws_i].y_line = yrow;
    tiledraws[tiledraws_i].tile_id = tilenum;
    tiledraws_c++;
    tiledraws_i++;
    return 1;
}

void CVort_engine_clearOverlay(void) {
    spritedraws_c = 0;
    bmpdraws_c    = 0;
    tiledraws_c   = 0;
}

void CVort_engine_syncDrawing(void) {
    /* Port of the SDL-path syncDrawing (see render/gfx.c). The intro and
     * wait loops rely on this to pace the game AND pump input: without
     * delayInGameTicks, updateInputStatus never runs, so GBA buttons
     * never make it to g_input, and g_game.sprite_sync stays 0 so
     * introTickCounter never decrements. Tear protection is handled by
     * VBlankIntrWait inside CK_PlatformSleepMs / doWaitInterval. */
    tiledraws_c = bmpdraws_c = spritedraws_c = 0;
    spritedraws_i = 0;
    bmpdraws_i = 0;
    tiledraws_i = 0;
    screentiles_i = 0;

    /* GBA runs slower than the 146 Hz PIT tick, so delayInGameTicks takes
     * the "already late" early-return every frame and never pumps input.
     * Call updateInputStatus explicitly so buttons are read at least once
     * per frame regardless of the delay path. */
    CVort_engine_updateInputStatus();
    CVort_engine_delayInGameTicks(ticks_sync, 6);
    if (engine_arguments.extras.vorticonsDemoModeToggle) {
        g_game.sprite_sync = 6;
        ticks_sync += 6;
        CVort_private_engine_setTicks(ticks_sync);
    } else {
        g_game.sprite_sync = (CVort_private_engine_getTicks() & 0xFFFF)
                           - (ticks_sync & 0xFFFF);
        if (g_game.sprite_sync > 15) g_game.sprite_sync = 15;
        ticks_sync = CVort_private_engine_getTicks();
    }
}

void CVort_engine_drawScreen(void) {
    /* Mirrors the DOS/SDL path: walk the queued draws, invoke the
     * optional draw_func set by callers like do_draw_mural, then push
     * the resulting EGA buffer out to the GBA LCD. */
    CVort_engine_doDrawing();
    engine_isFrameReadyToDisplay = true;
    CVort_engine_updateActualDisplay();
}

void CVort_engine_doDrawing(void) {
    /* Keep the animation phase in sync with the SDL path. gameplay.c
     * builds anim_frame_tiles with four frames; anim_plane_i selects
     * the one for this tick. */
    anim_plane_i = (((CVort_private_engine_getTicks() & 0xFFFF)
                     >> g_game.anim_speed) & 6) >> 1;
    if (anim_plane_i < 0 || anim_plane_i >= 4) anim_plane_i = 0;

    /* SDL's drawScreen calls adaptiveTileRefresh to blit the visible
     * tile grid from map_data_tiles into byteEgaMemory every frame;
     * without the equivalent call here the worldmap / gameplay map
     * never reaches the framebuffer and any box-draw overlay (such as
     * the "Keens Left" intro) stays on screen forever. */
    CVort_engine_adaptiveTileRefresh(0);

    /* Walk the queued draw lists and emit into byteEgaMemory. Counters
     * are reset at the START of each frame by syncDrawing (matching the
     * SDL convention), so we leave them intact here.
     *
     * The engine (see gfx.c:3123 / 3138) bakes a +4 byte / +0x20 pixel
     * panning margin into every tile/sprite xbyte/y_line value so that
     * the DOS EGA panning registers land on the visible origin. The GBA
     * output path samples byteEgaMemory from (0,0) and doesn't apply
     * pel_panning, so we must back that offset out here — otherwise all
     * queued draws (HUD, enemies, Keen) end up shifted 32 px right /
     * 32 px down and the left edge of the framebuffer is never
     * touched. */
    for (uint16_t i = 0; i < tiledraws_c && i < 0x64; i++) {
        const TileDraw_T *d = &tiledraws[i];
        int xb = (int)d->x_byte - 4;
        int yl = (int)d->y_line - 0x20;
        if (xb < 0 || yl < 0) continue;
        CVort_engine_drawTile((uint16_t)xb, (uint16_t)yl, d->tile_id);
    }
    for (uint16_t i = 0; i < bmpdraws_c && i < 0xa; i++) {
        const BmpDraw_T *d = &bmpdraws[i];
        CVort_engine_drawBitmap(d->x, d->y, d->bmp_loc);
    }
    for (uint16_t i = 0; i < spritedraws_c && i < 0x1f4; i++) {
        const SpriteDraw_T *d = &spritedraws[i];
        int xb = (int)d->x_byte - 4;
        int yr = (int)d->y_row - 0x20;
        if (xb < 0 || yr < 0) continue;
        CVort_engine_drawSprite((uint16_t)xb, (uint16_t)yr, d->sprite_copy);
    }
    if (draw_func) {
        (draw_func)();
        /* draw_func (e.g. do_draw_mural) writes into byteEgaMemory
         * without telling us where — repaint everything next frame. */
        gba_atrInvalidateAll();
    }
    engine_isFrameReadyToDisplay = true;
}

void CVort_engine_blitTile(uint16_t num, uint32_t firstPos) {
    const uint8_t *src = gba_decodeTile(num);
    if (!src) return;
    if (firstPos + 16 * ENGINE_EGA_GFX_SCANLINE_LEN >
        sizeof(engine_screen.client.byteEgaMemory)) return;
    uint8_t *dst = engine_screen.client.byteEgaMemory + firstPos;
    gba_blitIndexed8(dst, src, 16, 16,
                     ENGINE_EGA_GFX_SCANLINE_LEN, 16);
    /* firstPos is a raw buffer offset; rather than reverse it into
     * page/cell coordinates, repaint everything. Rare call. */
    gba_atrInvalidateAll();
}

void CVort_engine_adaptiveTileRefresh(uint16_t initTileIndex) {
    /* Port of the SDL path (gfx.c:3313). The original walks a 21x14
     * tile grid at the current scroll origin and blits via an ATR
     * dirty cache; we skip the cache and redraw the visible grid in
     * full every frame. Tile coordinates in byteEgaMemory are aligned
     * to 16-pixel tile boundaries — sub-tile panning is unused on the
     * GBA output path (updateActualDisplay scales the 320x200 frame
     * without honouring pel_panning). */
    (void)initTileIndex;
    if (!map_data_tiles || map_width_tile == 0 || map_height_tile == 0) return;
    if (!anim_plane[anim_plane_i]) return;

    const int32_t scrollXTile = (int32_t)((scroll_x & 0xFFFFFF) >> 12);
    const int32_t scrollYTile = (int32_t)((scroll_y & 0xFFFFFF) >> 12);
    /* 21x13 covers 336x208, which fully encloses the 320x200 visible
     * region plus one tile of slack for sub-tile scroll. */
    const uint16_t *animTable = anim_plane[anim_plane_i];
    const uint16_t tileCap = engine_egaHeadGeneral.tileNum;

    /* Dirty-cell tracking: repaint the page in full when its scroll
     * origin changed (or after an invalidation); otherwise only blit
     * cells whose resolved tile differs from what's already there —
     * animated tiles and cells trampled by sprites/text last frame. */
    const int page = gba_atrPage();
    uint16_t *owner = s_gbaAtrOwner[page];
    if (!s_gbaAtrValid[page]
        || s_gbaAtrScrollX[page] != scrollXTile
        || s_gbaAtrScrollY[page] != scrollYTile) {
        for (int i = 0; i < GBA_ATR_ROWS * GBA_ATR_COLS; i++) {
            owner[i] = GBA_ATR_NONE;
        }
        s_gbaAtrScrollX[page] = scrollXTile;
        s_gbaAtrScrollY[page] = scrollYTile;
        s_gbaAtrValid[page] = true;
    }

    for (int ty = 0; ty < GBA_ATR_ROWS; ty++) {
        const int mapY = scrollYTile + ty;
        if (mapY < 0 || mapY >= map_height_tile) continue;
        const int rowBase = mapY * (int)map_width_tile;
        uint16_t *ownerRow = owner + ty * GBA_ATR_COLS;
        for (int tx = 0; tx < GBA_ATR_COLS; tx++) {
            const int mapX = scrollXTile + tx;
            if (mapX < 0 || mapX >= map_width_tile) continue;
            const uint16_t raw = (uint16_t)map_data_tiles[rowBase + mapX];
            if (raw >= tileCap) continue;
            const uint16_t animTile = animTable[raw];
            if (ownerRow[tx] == animTile) continue;
            /* drawTile expects x in byte-columns (one tile = 2 bytes)
             * and y in raw pixel rows. */
            if (gba_drawTileCommon((uint16_t)(tx * 2), (uint16_t)(ty * 16),
                                   animTile)) {
                ownerRow[tx] = animTile;
            }
        }
    }
    engine_isFrameReadyToDisplay = true;
}

void CVort_engine_scrollText(int16_t top_line_offs, int16_t bot_line_offs, int16_t direction) {
    (void)top_line_offs; (void)bot_line_offs; (void)direction;
}

void CVort_engine_egaPageFlip(void) {
    engine_currPage ^= 1;
    engine_currPageStart = engine_currPage ? 0x3000 : 0;
    engine_dstPage       = engine_currPage ? 0 : 0x3000;
}

void CVort_engine_setBorderColor(uint8_t color) {
    engine_screen.client.currParsedBorderColor = color & 0x0F;
    /* BG palette index 0 is the backdrop. */
    BG_COLORS[0] = rgb888_to_bgr555(engine_egaRGBColorTable[color & 0x0F]);
}

void CVort_engine_gotoXY(uint8_t x, uint8_t y) {
    if (x == 0 || y == 0) return;
    x--; y--;
    if (x >= ENGINE_EGAVGA_TXT_COLS_NUM || y >= ENGINE_EGAVGA_TXT_ROWS_NUM) return;
    engine_screen.client.txtCursorPosX = x;
    engine_screen.client.txtCursorPosY = y;
}

void CVort_engine_setPaletteAndBorderColor(const uint8_t *palette) {
    CVort_engine_setBorderColor(palette[16]);
    for (int i = 0; i < 16; i++) {
        /* palette[i] is the EGA signal nibble; the 16-entry RGB
         * lookup is its interpretation. */
        uint8_t idx = palette[i] & 0x0F;
        engine_screen.client.currParsedPalette[i] = idx;
        BG_COLORS[i] = rgb888_to_bgr555(engine_egaRGBColorTable[idx]);
    }
    engine_isFrameReadyToDisplay = true;
}

void CVort_engine_showImageFile(const char *filename) {
    (void)filename;
    scroll_x = scroll_y = 0;
    engine_isFrameReadyToDisplay = true;
}
