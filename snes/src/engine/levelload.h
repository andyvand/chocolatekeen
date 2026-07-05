/* Level loading: CRLE-expand a ROM level blob into WRAM bank $7F and
 * publish the map globals the game logic uses (Keen layout: header words,
 * tile plane, sprite plane).
 */
#ifndef CK_SNES_LEVELLOAD_H
#define CK_SNES_LEVELLOAD_H

#include <snes.h>

/* Whole expanded level lives in WRAM bank $7F (64 KB), declared as a
 * linker RAM section in ram7f.asm so C gets a proper far symbol. Layout
 * after the leading-size-word drop matches the desktop engine:
 *   [0]=width tiles, [1]=height tiles, [7]=plane size bytes,
 *   tiles plane at word 16, sprite plane at word 16 + [7]/2.
 */
extern u16 ck_map_data[];

extern u16 *map_data_tiles;    /* far pointer into bank $7F */
extern u16 *map_data_sprites;
extern u16 map_width_tile;
extern u16 map_height_tile;
extern u16 ck_rowofs[256];     /* y -> y * map_width_tile (word index) */

/* Loads level by Keen number (1..16, 80 = world map).
 * Returns 0 on success, 1 if the level is not in ROM. Leaves the screen
 * forced-blank state untouched; caller drives display. */
u8 ck_level_load(u8 levelNum);

/* ROM directory lookup (NULL if absent) */
const struct CkLevelEntry_T *ck_level_find(u8 levelNum);

/* Far-pointer offset with bank carry.
 *
 * 816-tcc LANDMINE: casting a pointer to u32 ("(u32)(const u8 *)sym")
 * lowers through a 16-bit int and SIGN-EXTENDS the pointer's low word,
 * dropping the bank byte - so the old "(u32)ptr + ofs" workaround reads
 * the wrong bank whenever the base symbol lives above $8000 in its
 * bank. This helper keeps the pointer intact in a union and does real
 * 32-bit addition (low-word add carries into the bank byte, valid for
 * blobs laid out in consecutive HiROM banks). */
const u8 *ck_far_ofs(const u8 *base, u32 ofs);

#endif
