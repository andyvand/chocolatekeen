/* SNES pad -> Vorticons game input. Mirrors the shape the game logic
 * reads (direction + jump/pogo/fire button states and one-shot presses).
 *
 * Mapping: d-pad = move, B = jump (Ctrl), Y = pogo (Alt), X or B+Y = fire,
 * Start = status/pause, Select = menu key. (Fire in Vorticons is
 * jump+pogo together, kept here as its own flag too.)
 */
#ifndef CK_SNES_INPUT_H
#define CK_SNES_INPUT_H

#include <snes.h>

typedef struct CkInput_T {
    u8 left, right, up, down;
    u8 jump, pogo, fire;
    u8 jump_pressed, pogo_pressed, fire_pressed; /* new this frame */
    u8 start_pressed, select_pressed;
    u8 l_pressed, r_pressed;    /* shoulder buttons (debug/scripted) */
} CkInput;

extern CkInput g_ck_input;

void ck_input_init(void);
void ck_input_update(void);

#endif
