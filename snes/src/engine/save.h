/* Battery-SRAM save layer (8 KB, CARTRIDGETYPE $02 / SRAMSIZE $03).
 * Layout:
 *   0x0000  16-byte header: "CKS1" magic, format version, episode
 *   0x0010  9 slots x 128 bytes (CkSaveSlot serialized + checksum)
 *   0x0500  high scores / config (M6+)
 * All access goes through pvsneslib consoleCopySramWithOffset /
 * consoleLoadSramWithOffset (library handles the bank window).
 */
#ifndef CK_SNES_SAVE_H
#define CK_SNES_SAVE_H

#include <snes.h>

#define CK_SAVE_SLOTS 9

typedef struct CkSaveSlot_T {
    u16 stuff[9];        /* keen_gp.stuff: pogo, keys, ... */
    s16 lives;
    u16 ammo;
    s32 score;
    s32 extra_life_pts;
    u16 doneLevels;      /* bitmask of completed levels 1..16 */
    u16 targetsMask;     /* ep2: bitmask of saved cities (targets[0..7]) */
    u16 worldX, worldY;  /* Keen's world map tile position */
    u8  levelNum;        /* level in progress (0 = on map) */
    u8  pad;
    u16 checksum;        /* 16-bit sum of all prior bytes */
} CkSaveSlot;

/* Formats the header if magic is absent (first boot / corrupt). */
void ck_save_init(void);

/* Returns 1 if slot holds a valid save. slot: 0..CK_SAVE_SLOTS-1 */
u8 ck_save_slot_used(u8 slot);

/* Returns 1 on success (valid checksum). */
u8 ck_save_read(u8 slot, CkSaveSlot *out);

void ck_save_write(u8 slot, const CkSaveSlot *in);

void ck_save_erase(u8 slot);

#endif
