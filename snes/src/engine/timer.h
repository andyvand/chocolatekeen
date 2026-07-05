/* Game-tick pacing. The DOS engine runs off the 145.65 Hz PIT tick and
 * paces sprites with sprite_sync = elapsed ticks per frame, clamped to 15
 * (see src/render/gfx.c syncDrawing and the GBA port in gfx_gba.c).
 *
 * On SNES we accumulate ticks in 8.8 fixed point per video frame:
 *   NTSC: 145.65 / 60.10 Hz = 2.4235 ticks/frame -> 620/256
 *   PAL:  145.65 / 50.01 Hz = 2.9124 ticks/frame -> 746/256
 * Lag frames naturally increase the next frame's tick count, matching the
 * DOS engine's behavior of measuring real elapsed ticks.
 */
#ifndef CK_SNES_TIMER_H
#define CK_SNES_TIMER_H

#include <snes.h>

extern u16 ck_sprite_sync;   /* game ticks to advance this frame (0..15) */
extern u32 ck_ticks;         /* running tick counter (getTicks analog)   */

void ck_timer_init(void);

/* Call once per rendered frame (after WaitForVBlank). Updates ck_ticks
 * and ck_sprite_sync. framesElapsed is normally 1; pass the real count
 * after lag frames if known. */
void ck_timer_frame(u8 framesElapsed);

#endif
