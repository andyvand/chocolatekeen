#include "engine/render.h"
#include "engine/levelload.h"
#include "data_format.h"
#include "snes_data_gen.h"   /* ck_tiles_chr */

u16 ck_cam_px;
u16 ck_cam_py;
u16 ck_cam_px_max;
u16 ck_cam_py_max;
const u16 *ck_render_tset; /* set by caller before ck_render_level_init */
volatile u16 ck_dbg_fill_px, ck_dbg_fill_py; /* fill-time camera (debug) */

/* global tile id -> VRAM base char (slot*4); 720 covers ep3's 715 TILENUM */
static u16 s_tileLut[720];

/* Strip staging: one column (32 world tiles tall doesn't exist; the entry
 * map is 32 entries tall = 16 world tiles... entries) and one row.
 * A world-tile column strip = 2 entry columns x 32 entry rows.
 * A world-tile row strip    = 2 entry rows x 64 entry columns.
 */
/* Strip rings: staged strips persist until FULLY flushed inside real
 * vblank time (VRAM writes outside vblank are silently dropped by the
 * PPU - flushing must be gated on REG_HVBJOY bit7, exactly like the
 * sprite chr uploads in msprite.c). 4 entries per axis is ample: one
 * crossing happens at most every 8 frames at max camera speed and a
 * flush drains within 1-2 vblanks. */
#define CK_STRIPQ 6
static u16 s_colStrip[CK_STRIPQ][2][32]; /* [slot][entry col parity][row] */
static u16 s_colWorldX[CK_STRIPQ];       /* 0xFFFF = slot free            */
static u8  s_colHead;                    /* next slot to flush            */
static u8  s_colCount;
static u16 s_rowStrip[CK_STRIPQ][2][64];
static u16 s_rowWorldY[CK_STRIPQ];
static u8  s_rowHead;
static u8  s_rowCount;

static u16 s_lastTileX;       /* camera world-tile position last frame     */
static u16 s_lastTileY;

/* ---- tile animation (TILEINFO_Anim chains) ---------------------------
 * The DOS engine never mutates map_data for animation; its adaptive
 * refresh redraws visible tiles through anim_plane[phase][tile]. Here we
 * keep per-tile chain position/length tables and apply the mapping in
 * ck_map_entry, so strips, dirty tiles and the phase rescan all agree.
 */
static u8 s_animIdx[720];   /* position of tile inside its anim chain    */
static u8 s_animLen[720];   /* chain length: 1, 2 or 4                   */
static u8 s_animPhase;      /* 0..3                                      */
static u8 s_animScanRow;    /* rescan cursor (0..15), 0xFF = idle        */

void ck_render_anim_init(const u16 *animTab, u16 tileNum)
{
    u16 t = 0;
    if (tileNum > 720)
        tileNum = 720;
    /* runtime init: RAM is not zero-initialized on this target */
    for (t = 0; t < 720; t++) {
        s_animIdx[t] = 0;
        s_animLen[t] = 1;
    }
    t = 0;
    while (t < tileNum) {
        u16 n = animTab[t];
        if (n == 2 || n == 4) {
            u16 k;
            for (k = 0; k < n; k++) {
                if (t + k >= tileNum)
                    break;
                s_animIdx[t + k] = (u8)k;
                s_animLen[t + k] = (u8)n;
            }
            t += n;
        } else {
            t++;
        }
    }
    s_animPhase = 0;
    s_animScanRow = 0xFF;
}

static u16 ck_anim_tile(u16 t)
{
    u8 len = s_animLen[t];
    u8 idx;
    if (len < 2)
        return t;
    idx = s_animIdx[t];
    return (u16)((u16)(t - idx) + (u16)((u8)(idx + s_animPhase) & (len - 1)));
}

/* Tilemap entry for one 8x8 quadrant of a world tile (palette 0, prio 0). */
static u16 ck_map_entry(u16 worldTx, u16 worldTy, u8 quad)
{
    u16 t = 0;
    if (worldTx < map_width_tile && worldTy < map_height_tile)
        t = map_data_tiles[ck_rowofs[worldTy] + worldTx];
    if (t >= 720)
        t = 0;
    t = ck_anim_tile(t);
    return (u16)(s_tileLut[t] + quad);
}

/* ---- dirty-tile queue -------------------------------------------------
 * Ring of pending 2x2 entry patches, flushed (rate-limited) in
 * ck_render_vblank. Worst-case burst is the ep1 shot-chain body
 * (27 tiles in one tick) plus a few animated tiles.
 */
#define CK_DIRTYQ 64
#define CK_DIRTY_FLUSH_MAX 24
static u16 s_dirtyE[CK_DIRTYQ][4]; /* entries TL,TR,BL,BR                */
static u16 s_dirtyA[CK_DIRTYQ][2]; /* VRAM word addr of top/bottom pairs */
static u16 s_dirtyHead;            /* next slot to flush                 */
static u16 s_dirtyCount;
volatile u16 ck_dbg_dirty_drop;    /* queue-overflow counter (debug)     */

void ck_render_tile_dirty(u16 tx, u16 ty)
{
    s16 dx, dy;
    u16 slot, ec, er, addr;

    /* Inside the loaded tilemap window? Columns span [camTx-8, camTx+23],
     * rows [camTy-1, camTy+14] (see the staging logic below). */
    dx = (s16)tx - (s16)(ck_cam_px >> 4);
    if (dx < -8)
        return;
    if (dx > 23)
        return;
    dy = (s16)ty - (s16)(ck_cam_py >> 4);
    if (dy < -1)
        return;
    if (dy > 14)
        return;

    if (s_dirtyCount >= CK_DIRTYQ) {
        ck_dbg_dirty_drop++;
        return;
    }
    slot = s_dirtyHead + s_dirtyCount;
    if (slot >= CK_DIRTYQ)
        slot -= CK_DIRTYQ;

    s_dirtyE[slot][0] = ck_map_entry(tx, ty, 0);
    s_dirtyE[slot][1] = ck_map_entry(tx, ty, 1);
    s_dirtyE[slot][2] = ck_map_entry(tx, ty, 2);
    s_dirtyE[slot][3] = ck_map_entry(tx, ty, 3);

    ec = (u16)((tx << 1) & 63);            /* even, so ec+1 shares screen */
    er = (u16)((ty << 1) & 31);
    addr = (u16)(CK_BG1_MAP_VRAM + (ec & 31) + ((ec & 32) ? 0x400 : 0));
    s_dirtyA[slot][0] = (u16)(addr + (er << 5));
    s_dirtyA[slot][1] = (u16)(addr + ((er + 1) << 5));
    s_dirtyCount++;
}

void ck_map_set(u16 tx, u16 ty, u16 tile)
{
    map_data_tiles[ck_rowofs[ty] + tx] = tile;
    ck_render_tile_dirty(tx, ty);
}

void ck_render_set_anim_phase(u8 phase)
{
    phase &= 3;
    if (phase == s_animPhase)
        return;
    s_animPhase = phase;
    s_animScanRow = 0;      /* start rescanning the visible window */
}

/* Rescan a few window rows per frame after a phase change, re-queueing
 * tiles that belong to anim chains. */
static void ck_anim_scan(void)
{
    u16 camTx, camTy, wy, wx, x0, x1, t;
    u8 rows;
    if (s_animScanRow == 0xFF)
        return;
    camTx = ck_cam_px >> 4;
    camTy = ck_cam_py >> 4;
    x0 = (camTx >= 1) ? (u16)(camTx - 1) : 0;
    x1 = (u16)(camTx + 17);
    for (rows = 0; rows < 4; rows++) {
        if (s_animScanRow >= 16) {
            s_animScanRow = 0xFF;
            return;
        }
        if (s_dirtyCount >= CK_DIRTYQ - 20)
            return;         /* resume this row next frame */
        wy = (u16)(camTy + s_animScanRow);
        if (camTy >= 1)
            wy--;
        if (wy < map_height_tile) {
            const u16 *row = map_data_tiles + ck_rowofs[wy];
            for (wx = x0; wx <= x1; wx++) {
                if (wx >= map_width_tile)
                    break;
                t = row[wx];
                if (t < 720 && s_animLen[t] > 1)
                    ck_render_tile_dirty(wx, wy);
            }
        }
        s_animScanRow++;
    }
}

/* Stage a full vertical strip for world tile column wx covering the entry
 * map's 32 rows (16 world tiles). The row window is [camTy-1, camTy+14]:
 * the buffer is only 2 world rows taller than the 14-row viewport, so it
 * must stay centered on the camera (unlike columns, which have 8 tiles
 * of slack on either side). */
static u8 ck_stage_col(u16 wx)
{
    u16 wyTop = (ck_cam_py >> 4);
    u16 i, wy;
    u8 slot;
    /* Guard the column argument itself (initial fill / edge cameras):
     * an off-map column must stage its in-map alias instead. */
    if (wx >= map_width_tile) {
        if (wx >= 32)
            wx -= 32;
        if (wx >= map_width_tile)
            return 1;              /* nothing to stage = done */
    }
    if (s_colCount >= CK_STRIPQ)
        return 0;                  /* ring full: caller must retry */
    slot = (u8)(s_colHead + s_colCount);
    if (slot >= CK_STRIPQ)
        slot -= CK_STRIPQ;
    if (wyTop >= 1) wyTop -= 1; else wyTop = 0;
    for (i = 0; i < 16; i++) {
        wy = wyTop + i;
        /* An entry row serves world rows 16 apart. A row past the map
         * bottom must show its ALIAS's tiles (the in-map row 16 above),
         * or off-map staging poisons visible rows (cyan-border bug). */
        if (wy >= map_height_tile) {
            if (wy >= 16)
                wy -= 16;
        }
        s_colStrip[slot][0][((wy & 15) << 1)]     = ck_map_entry(wx, wy, 0);
        s_colStrip[slot][0][((wy & 15) << 1) + 1] = ck_map_entry(wx, wy, 2);
        s_colStrip[slot][1][((wy & 15) << 1)]     = ck_map_entry(wx, wy, 1);
        s_colStrip[slot][1][((wy & 15) << 1) + 1] = ck_map_entry(wx, wy, 3);
    }
    s_colWorldX[slot] = wx;
    s_colCount++;
    return 1;
}


/* Stage a full horizontal strip for world tile row wy covering the entry
 * map's 64 columns (32 world tiles) around the camera. */
static u8 ck_stage_row(u16 wy)
{
    u16 wxLeft = (ck_cam_px >> 4);
    u16 i, wx;
    u8 slot;
    /* Guard the row argument itself (initial fill on levels shorter
     * than the 16-row window, bottom cameras): stage the alias row. */
    if (wy >= map_height_tile) {
        if (wy >= 16)
            wy -= 16;
        if (wy >= map_height_tile)
            return 1;
    }
    if (s_rowCount >= CK_STRIPQ)
        return 0;
    slot = (u8)(s_rowHead + s_rowCount);
    if (slot >= CK_STRIPQ)
        slot -= CK_STRIPQ;
    if (wxLeft >= 8) wxLeft -= 8; else wxLeft = 0;
    for (i = 0; i < 32; i++) {
        wx = wxLeft + i;
        /* An entry column serves world columns 32 apart: past the right
         * map edge, show the alias 32 to the left (see ck_stage_col). */
        if (wx >= map_width_tile) {
            if (wx >= 32)
                wx -= 32;
        }
        s_rowStrip[slot][0][((wx & 31) << 1)]     = ck_map_entry(wx, wy, 0);
        s_rowStrip[slot][0][((wx & 31) << 1) + 1] = ck_map_entry(wx, wy, 1);
        s_rowStrip[slot][1][((wx & 31) << 1)]     = ck_map_entry(wx, wy, 2);
        s_rowStrip[slot][1][((wx & 31) << 1) + 1] = ck_map_entry(wx, wy, 3);
    }
    s_rowWorldY[slot] = wy;
    s_rowCount++;
    return 1;
}


/* One 64-byte VRAM run via raw channel-0 DMA (fast enough to fit many
 * runs in the post-ISR vblank remnant; the per-word C loop was not).
 * vmain 0x80 = +1 word, 0x81 = +32 words (vertical tilemap runs). */
static void bg_run_dma(const u16 *src, u16 vramWord, u8 vmain, u16 bytes)
{
    union { const u16 *p; u8 b[4]; } fp;
    fp.p = src;
    REG_VMAIN = vmain;
    REG_VMADDLH = vramWord;
    REG_DMAP0 = 0x01;
    REG_BBAD0 = 0x18;
    REG_A1T0LH = (u16)(((u16)fp.b[1] << 8) | fp.b[0]);
    REG_A1B0 = fp.b[2];
    REG_DAS0LH = bytes;
    REG_MDMAEN = 0x01;
}

/* Flush one staged column strip (2 vertical runs, stride 32 words). */
static void ck_flush_one_col(void)
{
    u8 slot = s_colHead;
    u16 wx = s_colWorldX[slot];
    u16 p, ec, addr;
    for (p = 0; p < 2; p++) {
        ec = (u16)(((wx << 1) + p) & 63);
        addr = (u16)(CK_BG1_MAP_VRAM + (ec & 31) + ((ec & 32) ? 0x400 : 0));
        bg_run_dma(s_colStrip[slot][p], addr, 0x81, 64);
    }
    REG_VMAIN = 0x80;
    s_colWorldX[slot] = 0xFFFF;
    s_colHead++;
    if (s_colHead >= CK_STRIPQ)
        s_colHead = 0;
    s_colCount--;
}

/* Flush one staged row strip (2 entry rows x 2 screens = 4 runs). */
static void ck_flush_one_row(void)
{
    u8 slot = s_rowHead;
    u16 wy = s_rowWorldY[slot];
    u16 p, er, addr;
    for (p = 0; p < 2; p++) {
        er = (u16)(((wy << 1) + p) & 31);
        addr = (u16)(CK_BG1_MAP_VRAM + (er << 5));
        bg_run_dma(&s_rowStrip[slot][p][0], addr, 0x80, 64);
        bg_run_dma(&s_rowStrip[slot][p][32], (u16)(addr + 0x400), 0x80, 64);
    }
    s_rowWorldY[slot] = 0xFFFF;
    s_rowHead++;
    if (s_rowHead >= CK_STRIPQ)
        s_rowHead = 0;
    s_rowCount--;
}

void ck_render_level_init(void)
{
    const u16 *tset = ck_render_tset;

    u16 count, i, g, wy;
    u16 wpx, hpx;

    /* Session reset: pending strips/dirty patches/anim rescans belong
     * to the PREVIOUS session (their window tests used the old camera -
     * e.g. mark_cities_done stamps city tiles before the map camera
     * exists). The full window fill below makes dropping them correct. */
    s_dirtyHead = 0;
    s_dirtyCount = 0;
    s_animScanRow = 0xFF;
    s_colHead = s_colCount = 0;
    s_rowHead = s_rowCount = 0;
    for (i = 0; i < CK_STRIPQ; i++) {
        s_colWorldX[i] = 0xFFFF;
        s_rowWorldY[i] = 0xFFFF;
    }

    /* Palette: EGA identity into BG palette 0. */
    dmaCopyCGram((u8 *)ck_pal_ega, 0, 32);

    /* Build LUT: everything unmapped -> slot 0. */
    for (i = 0; i < 720; i++)
        s_tileLut[i] = 0;
    count = tset[0];
    for (i = 0; i < count; i++) {
        g = tset[1 + i];
        if (g < 720)
            s_tileLut[g] = (u16)(i << 2);
        /* Upload the 4 chars (128 bytes) for this tile to slot i.
         * tiles.chr spans two HiROM banks; ck_far_ofs keeps the bank
         * byte correct for tiles >= 512 ((u32)ptr casts drop it). */
        dmaCopyVram((u8 *)ck_far_ofs(ck_tiles_chr, (u32)g << 7),
                    (u16)(CK_BG1_CHR_VRAM + (i << 6)), 128);
    }

    /* Clamp bounds. */
    wpx = (u16)(map_width_tile << 4);
    hpx = (u16)(map_height_tile << 4);
    ck_cam_px_max = (wpx > 256) ? (u16)(wpx - 256) : 0;
    ck_cam_py_max = (hpx > 224) ? (u16)(hpx - 224) : 0;
    if (ck_cam_px > ck_cam_px_max) ck_cam_px = ck_cam_px_max;
    if (ck_cam_py > ck_cam_py_max) ck_cam_py = ck_cam_py_max;

    /* Initial fill: 32 world cols x 16 world rows around the camera,
     * written directly (forced blank assumed). */
    {
        u16 wyTop = ck_cam_py >> 4;
        if (wyTop >= 1) wyTop -= 1; else wyTop = 0;
        for (i = 0; i < 16; i++) {
            wy = wyTop + i;
            ck_stage_row(wy);
            while (s_rowCount)
                ck_flush_one_row();  /* forced blank: no gating needed */
        }
        s_colHead = s_colCount = 0;
        for (i = 0; i < CK_STRIPQ; i++)
            s_colWorldX[i] = 0xFFFF;
    }

    s_dirtyHead = s_dirtyCount = 0;
    ck_dbg_dirty_drop = 0;
    s_animScanRow = 0xFF;

    s_lastTileX = ck_cam_px >> 4;
    s_lastTileY = ck_cam_py >> 4;
    ck_dbg_fill_px = ck_cam_px;   /* camera the fill was painted for */
    ck_dbg_fill_py = ck_cam_py;

    bgSetEnable(0);      /* UI screens (menus/title) may have hidden BG1 */
    bgSetScroll(0, ck_cam_px & 511, (ck_cam_py - 1) & 255);
}

void ck_render_update(void)
{
    u16 tx = ck_cam_px >> 4;
    u16 ty = ck_cam_py >> 4;

    /* Edge guards: staging a column/row outside the map would write
     * empty entries into slots that alias *visible* tiles (the entry
     * map wraps mod 32/16 world tiles). Off-map margins keep their
     * stale (still valid) content instead. */
    /* The maintained window is EXACTLY [tx-8..tx+23] x [ty-1..ty+14]
     * (32x16 world tiles = the whole entry map). Crossing triggers must
     * stage exactly the incoming edge of that window - staging one tile
     * beyond it (the old tx+24) leaves a permanently stale line that
     * scrolls into view later (user-visible corruption). Out-of-map /
     * underflowed arguments are alias-guarded inside the stagers. */
    if (tx != s_lastTileX) {
        u8 ok;
        if (tx > s_lastTileX)
            ok = ck_stage_col((u16)(tx + 23)); /* entering from right */
        else
            ok = ck_stage_col((u16)(tx - 8));  /* entering from left  */
        if (ok)
            s_lastTileX = tx;    /* else: retry next frame (ring full) */
    }
    if (ty != s_lastTileY) {
        u8 ok;
        if (ty > s_lastTileY)
            ok = ck_stage_row((u16)(ty + 14)); /* new bottom margin  */
        else
            ok = ck_stage_row((u16)(ty - 1));  /* new top margin row */
        if (ok)
            s_lastTileY = ty;
    }

    ck_anim_scan();
}

static void ck_flush_dirty(void)
{
    u16 n = CK_DIRTY_FLUSH_MAX;
    REG_VMAIN = 0x80;               /* word inc +1 */
    while (s_dirtyCount) {
        if (!n)
            break;
        if (!(REG_HVBJOY & 0x80))
            break;                  /* out of vblank: rest next frame */
        REG_VMADDLH = s_dirtyA[s_dirtyHead][0];
        REG_VMDATALH = s_dirtyE[s_dirtyHead][0];
        REG_VMDATALH = s_dirtyE[s_dirtyHead][1];
        REG_VMADDLH = s_dirtyA[s_dirtyHead][1];
        REG_VMDATALH = s_dirtyE[s_dirtyHead][2];
        REG_VMDATALH = s_dirtyE[s_dirtyHead][3];
        s_dirtyHead++;
        if (s_dirtyHead >= CK_DIRTYQ)
            s_dirtyHead = 0;
        s_dirtyCount--;
        n--;
    }
}

void ck_render_vblank(void)
{
    /* Scroll registers first (cheap, latched for this frame), then
     * drain the strip rings and dirty queue for as long as the live
     * vblank flag ($4212 bit7) says there is real vblank time left.
     * Undrained work persists to the next frame. */
    bgSetScroll(0, ck_cam_px & 511, (ck_cam_py - 1) & 255);

    while (s_colCount) {
        if (!(REG_HVBJOY & 0x80))
            return;
        ck_flush_one_col();
    }
    while (s_rowCount) {
        if (!(REG_HVBJOY & 0x80))
            return;
        ck_flush_one_row();
    }
    ck_flush_dirty();
}
