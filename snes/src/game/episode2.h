/* episode2.h - SNES transcription of src/episodes/episode2.c (enemy
 * spawns/thinks/contacts, Keen contact handler, tantalus bodies, the
 * lights-out palette mechanic, the earth-explode ending) plus the
 * ep2-relevant shared enemy code from src/game/enemies.c (Vorticon,
 * Vorticon youth, tank shot). Function pointers are CK_*_ ids.
 */
#ifndef CK_SNES_GAME_EPISODE2_H
#define CK_SNES_GAME_EPISODE2_H

#include <snes.h>
#include "game/sprites.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2

/* spawn-plane value -> sprite add (CVort2_init_level inner switch) */
void CVort2_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY);

/* episode contact_keen */
void CVort2_contact_keen(CkSprite *keen, CkSprite *contacted);

/* thinks (run on g_entities.temp_sprite) */
void CVort_think_vorticon_walk(void);
void CVort_think_vorticon_jump(void);
void CVort_think_vorticon_search(void);
void CVort_think_youth_walk(void);
void CVort_think_youth_jump(void);
void CVort2_think_elite_walk(void);
void CVort2_think_elite_shoot(void);
void CVort2_think_elite_jump(void);
void CVort2_think_guardbot_move(void);
void CVort2_think_guardbot_shoot(void);
void CVort2_think_guardbot_turn(void);
void CVort2_think_scrub_walk_left(void);
void CVort2_think_scrub_walk_down(void);
void CVort2_think_scrub_walk_right(void);
void CVort2_think_scrub_walk_up(void);
void CVort2_think_scrub_fall(void);
void CVort2_think_platform_move(void);
void CVort2_think_platform_turn(void);
void CVort2_think_tantalus(void);

/* contacts */
void CVort_contact_vorticon(CkSprite *vorticon, CkSprite *contacted);
void CVort_contact_tankshot(CkSprite *tankshot, CkSprite *contacted);
void CVort_contact_youth(CkSprite *youth, CkSprite *contacted);
void CVort2_contact_elite(CkSprite *elite, CkSprite *contacted);
void CVort2_contact_guardbot(CkSprite *guardbot, CkSprite *contacted);
void CVort2_contact_scrub(CkSprite *scrub, CkSprite *contacted);
void CVort2_contact_tantalus(CkSprite *tantalus, CkSprite *contacted);

/* bodies */
void CVort2_body_destroy_tantalus(CkBody *tantalus);

/* ending: tantalus ray fired / out of lives -> the earth explodes
 * (CVort2_draw_earth_explode, simplified for the SNES renderer). */
void ck_ep2_earth_explode(void);

#endif /* episode 2 */

#endif
