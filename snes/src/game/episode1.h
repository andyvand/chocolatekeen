/* episode1.h - SNES transcription of src/episodes/episode1.c (enemy
 * spawns/thinks/contacts, Keen contact handler, ep1 bodies) plus the
 * shared enemy code from src/game/enemies.c (Vorticon, tank shot).
 * Think/contact/body function pointers are CK_*_ ids (sprites.h).
 */
#ifndef CK_SNES_GAME_EPISODE1_H
#define CK_SNES_GAME_EPISODE1_H

#include <snes.h>
#include "game/sprites.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1

/* spawn-plane value -> sprite/body add (CVort1_init_level inner switch) */
void CVort1_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY);

/* episode contact_keen (what happens when things touch Keen) */
void CVort1_contact_keen(CkSprite *keen, CkSprite *contacted);

/* thinks (run on g_entities.temp_sprite) */
void CVort1_think_keen_frozen(void);
void CVort1_think_yorp_walk(void);
void CVort1_think_yorp_look(void);
void CVort1_think_yorp_stunned(void);
void CVort1_think_garg_move(void);
void CVort1_think_garg_look(void);
void CVort1_think_butler_walk(void);
void CVort1_think_butler_turn(void);
void CVort1_think_tankbot_move(void);
void CVort1_think_tankbot_spawn(void);
void CVort1_think_tankbot_turn(void);
void CVort1_think_tankbot_shoot(void);
void CVort1_think_cube(void);
void CVort1_think_cubette_flight(void);
void CVort_think_vorticon_walk(void);
void CVort_think_vorticon_jump(void);
void CVort_think_vorticon_search(void);

/* contacts */
void CVort1_contact_yorp(CkSprite *yorp, CkSprite *contacted);
void CVort1_contact_garg(CkSprite *garg, CkSprite *contacted);
void CVort1_contact_butler(CkSprite *butler, CkSprite *contacted);
void CVort1_contact_cube(CkSprite *cube, CkSprite *contacted);
void CVort1_contact_chain(CkSprite *chain, CkSprite *contacted);
void CVort_contact_vorticon(CkSprite *vorticon, CkSprite *contacted);
void CVort_contact_tankshot(CkSprite *tankshot, CkSprite *contacted);

/* bodies */
void CVort1_body_ice_cannon(CkBody *cannon);
void CVort1_body_shot_chain(CkBody *chain);

#endif /* episode 1 */

#endif
