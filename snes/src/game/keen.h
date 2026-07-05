/* Player (Keen) think states + player shot, transcribed from
 * src/game/enemies.c (CVort_think_keen_* / keengun / zapzot). Dispatch
 * is by CK_THINK_* id through ck_game_dispatch_think (gameplay.c).
 */
#ifndef CK_SNES_GAME_KEEN_H
#define CK_SNES_GAME_KEEN_H

#include <snes.h>

#include "game/sprites.h"

void CVort_think_keen_ground(void);
void CVort_think_keen_jump_ground(void);
void CVort_think_keen_jump_air(void);
void CVort_think_keen_shoot(void);
void CVort_think_keen_pogo_air(void);
void CVort_think_keen_pogo_ground(void);
void CVort_think_keen_exit(void);
void CVort_think_keen_death(void);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE != 1
void CVort_think_keen_stunned(void);
#endif
void CVort_think_zapzot(void);
void CVort_think_keengun(void);

void CVort_add_sprite_keengun(s32 pos_x, s32 pos_y);
void CVort_contact_keengun(struct CkSprite_T *keengun,
                           struct CkSprite_T *contacted);

#endif
