/* episode3.h - SNES transcription of src/episodes/episode3.c (enemy
 * spawns/thinks/contacts, Keen contact handler, enemy gun / Mangling
 * Machine bodies, grand-intellect intro) plus the ep3-relevant shared
 * enemy code from src/game/enemies.c (Vorticon, Vorticon youth).
 * Function pointers are CK_*_ ids (sprites.h).
 */
#ifndef CK_SNES_GAME_EPISODE3_H
#define CK_SNES_GAME_EPISODE3_H

#include <snes.h>
#include "game/sprites.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3

/* spawn-plane value -> sprite/body add (CVort3_init_level inner switch) */
void CVort3_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY);

/* episode contact_keen */
void CVort3_contact_keen(CkSprite *keen, CkSprite *contacted);

/* thinks (run on g_entities.temp_sprite) */
void CVort_think_vorticon_walk(void);
void CVort_think_vorticon_jump(void);
void CVort_think_vorticon_search(void);
void CVort_think_youth_walk(void);
void CVort_think_youth_jump(void);
void CVort3_think_vortimom_walk(void);
void CVort3_think_vortimom_shoot(void);
void CVort3_think_vortimomshot(void);
void CVort3_think_meep_walk(void);
void CVort3_think_meep_shoot(void);
void CVort3_think_meepshot(void);
void CVort3_think_vortininja_stand(void);
void CVort3_think_vortininja_jump(void);
void CVort3_think_foob_walk(void);
void CVort3_think_foob_run(void);
void CVort3_think_foob_scared(void);
void CVort3_think_jack(void);
void CVort3_think_ball(void);
void CVort3_think_platform_move(void);
void CVort3_think_platform_turn(void);
void CVort3_think_enemyshot(void);
void CVort3_think_spark(void);
void CVort3_think_heart(void);

/* contacts */
void CVort_contact_vorticon(CkSprite *vorticon, CkSprite *contacted);
void CVort_contact_youth(CkSprite *youth, CkSprite *contacted);
void CVort3_contact_vortimom(CkSprite *vortimom, CkSprite *contacted);
void CVort3_contact_vortimomshot(CkSprite *shot, CkSprite *contacted);
void CVort3_contact_meep(CkSprite *meep, CkSprite *contacted);
void CVort3_contact_vortininja(CkSprite *vortininja, CkSprite *contacted);
void CVort3_contact_foob(CkSprite *foob, CkSprite *contacted);
void CVort3_contact_spark(CkSprite *spark, CkSprite *contacted);
void CVort3_contact_heart(CkSprite *heart, CkSprite *contacted);

/* bodies */
void CVort3_think_enemygun_right(CkBody *enemygun);
void CVort3_think_enemygun_down(CkBody *enemygun);
void CVort3_think_mangling_arm(CkBody *arm);
void CVort3_think_mangling_leg_moving(CkBody *leg);
void CVort3_think_mangling_leg_awaiting(CkBody *leg);
void CVort3_think_mangling_arm_destruct(CkBody *body);
void CVort3_think_mangling_destruct(CkBody *body);

/* level 16 entry: the Grand Intellect reveal (text boxes on the SNES) */
void ck_ep3_grand_intellect(void);

#endif /* episode 3 */

#endif
