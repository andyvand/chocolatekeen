/* Metasprite drawing: maps baked CkSpriteFrame records (16x16/32x32 OAM
 * parts, chr streamed on demand) onto pvsneslib's dynamic sprite engine.
 *
 * Usage per frame:
 *   ck_msprite_begin();
 *   ck_msprite_draw(frameIdx, screenX, screenY);  (in Keen draw order)
 *   ck_msprite_end();                (before WaitForVBlank)
 *   ck_msprite_vblank();             (after WaitForVBlank: VRAM queue)
 */
#ifndef CK_SNES_MSPRITE_H
#define CK_SNES_MSPRITE_H

#include <snes.h>

/* OBJ chars live at VRAM $6000, small=16x16 large=32x32 (OBJSEL). */
#define CK_OBJ_CHR_VRAM 0x6000

void ck_msprite_init(void);
void ck_msprite_begin(void);
void ck_msprite_draw(u16 frameIdx, s16 screenX, s16 screenY);
void ck_msprite_end(void);
void ck_msprite_vblank(void);

/* Drain all pending chr uploads; only valid under forced blank. */
void ck_msprite_flush_all(void);

#endif
