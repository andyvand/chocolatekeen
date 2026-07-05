/* Transcription of src/game/physics.c (collision core, motion
 * integration, camera follow) for the SNES target. Function names are
 * kept CVort_* for diffability against the desktop tree. All functions
 * operate on g_entities.temp_sprite like the original.
 */
#ifndef CK_SNES_GAME_PHYSICS_H
#define CK_SNES_GAME_PHYSICS_H

#include <snes.h>
#include "game/sprites.h"

void CVort_move_left_right(s16 acceleration);
void CVort_pogo_jump(s16 max_height, s16 diff);
void CVort_check_ceiling(void);
void CVort_do_fall(void);
s16  CVort_compute_sprite_delta(void);
s16  CVort_check_ground(void);
void CVort_do_scrolling(void);
s16  CVort_sprite_active_screen(void);
s16  CVort_detect_sprite_col(CkSprite *spr_0, CkSprite *spr_1);
void CVort_kill_keen(void);
void CVort_kill_keen_temp(void);
void CVort_keen_bgtile_col(void);

void CVort_think_dead_sprite(void);
void CVort_think_kill_sprite(void);
void CVort_think_remove_sprite(void);

/* gameplay.c helpers used by physics/thinks */
void CVort_update_sprite_hitbox(void);
s16  CVort_add_sprite(void);
s16  CVort_add_body(void);

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE != 1
/* moving platforms / pushers (src/game/physics.c:343/438) */
void CVort_carry_keen(CkSprite *keen, CkSprite *carrier);
void CVort_push_keen(CkSprite *keen, CkSprite *pusher);
#endif

/* bodies + doors + switches (physics.c/ui.c on desktop) */
void CVort_open_door(s16 tileX, s16 tileY);
void CVort_toggle_switch(void);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
/* ep2 lights-out (src/game/ui.c CVort_lights_on/out; palette swap is
 * applied in vblank via ck_lights_vblank, episode2.c) */
void CVort_lights_on(void);
void CVort_lights_out(void);
void ck_lights_reset(void);   /* level start: lights back on, no queueing sound */
void ck_lights_vblank(void);  /* CGRAM flush, call right after WaitForVBlank */
#else
/* Episodes 1/3 have no lights mechanic, but shared flow code calls
 * these unconditionally. They MUST exist as no-ops here: without a
 * declaration, 816-tcc emits an implicit call to an unresolved symbol
 * = a wild jump at runtime (this corrupted the ep1 world map). */
#define CVort_lights_on()   ((void)0)
#define CVort_lights_out()  ((void)0)
#define ck_lights_reset()   ((void)0)
#define ck_lights_vblank()  ((void)0)
#endif
void CVort_body_slide_door(CkBody *door);
void CVort_body_bridge_extend(CkBody *bridge);
void CVort_body_bridge_retract(CkBody *bridge);

#endif
