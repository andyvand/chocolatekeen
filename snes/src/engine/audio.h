/* SPC700 beep-driver frontend. Uploads spc/driver.bin + the baked BRR
 * squares + ck_sounds_bin into SPC RAM via the IPL-ROM handshake at boot,
 * then plays Keen PC-speaker sounds by index over the port protocol
 * documented in spc/driver.asm.
 */
#ifndef CK_SNES_AUDIO_H
#define CK_SNES_AUDIO_H

#include <snes.h>

/* Returns 1 when the driver answered ready, 0 on handshake timeout
 * (sound stays disabled; game runs silent). */
u8 ck_audio_init(void);

/* Keen sound numbers are 1-based like the DOS engine (0 = no-op).
 * Applies the original priority gate against the currently playing
 * sound. */
void ck_audio_play(u16 sound);

void ck_audio_stop(void);

/* soundIndex+1 while a sound plays, 0 when idle. */
u8 ck_audio_status(void);

#endif
