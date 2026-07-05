/* BG1 playfield renderer: per-level tile preload into VRAM, global->VRAM
 * char LUT, initial tilemap fill and camera-driven column/row streaming.
 *
 * VRAM map (word addresses):
 *   $0000-$3FFF  BG1 chars: CK_VRAM_TILE_SLOTS 16x16 tiles x 4 chars
 *   $4000-$47FF  BG1 tilemap 64x32 (two 32x32 screens side by side)
 *   $4800-$4BFF  BG3 tilemap (later)
 *   $5000-$57FF  BG3 font 2bpp (later)
 *   $6000-$7FFF  OBJ chars (later)
 */
#ifndef CK_SNES_RENDER_H
#define CK_SNES_RENDER_H

#include <snes.h>

#define CK_BG1_CHR_VRAM 0x0000
#define CK_BG1_MAP_VRAM 0x4000

/* Camera position in world pixels (top-left of 256x224 viewport). */
extern u16 ck_cam_px;
extern u16 ck_cam_py;
extern u16 ck_cam_px_max;
extern u16 ck_cam_py_max;

/* Level tile preload list (CkLevelEntry.tset); set before
 * ck_render_level_init(). */
extern const u16 *ck_render_tset;

/* Call once after ck_level_load(), under forced blank:
 * uploads the level tile set + palette, builds the LUT, fills the
 * tilemap window around the camera, sets BG1 scroll. */
void ck_render_level_init(void);

/* Per-frame, before WaitForVBlank(): detect camera tile-boundary
 * crossings and prepare edge strips in WRAM. */
void ck_render_update(void);

/* Per-frame, right after WaitForVBlank(): write queued strips to VRAM
 * and latch the hardware scroll registers. */
void ck_render_vblank(void);

/* ---- map mutation / tile animation (M4) ------------------------------ */

/* Once at boot: build the TILEINFO_Anim chain tables (animsetup walk,
 * see src/game/gameplay.c:163). animTab = TILEINFO_Anim, tileNum =
 * CK_TILENUM. */
void ck_render_anim_init(const u16 *animTab, u16 tileNum);

/* Game code changed map_data_tiles[ty][tx]: if the tile is inside the
 * loaded tilemap window, queue its 2x2 BG1 entries for the next vblank
 * flush. */
void ck_render_tile_dirty(u16 tx, u16 ty);

/* Write helper: mutate the map and mark the tile dirty in one go. */
void ck_map_set(u16 tx, u16 ty, u16 tile);

/* Tile anim phase 0..3 (((ticks >> anim_speed) & 6) >> 1). On change the
 * visible window is rescanned over the next few frames and animated
 * tiles are re-queued. */
void ck_render_set_anim_phase(u8 phase);

#endif
