/* In-level game loop for the SNES port (structure of
 * src/game/gameplay.c CVort_draw_level, split into a per-frame call
 * driven by main). */
#ifndef CK_SNES_GAME_GAMEPLAY_H
#define CK_SNES_GAME_GAMEPLAY_H

#include <snes.h>

/* Reset entities, run the sprite-plane spawn pass (M3: Keen only,
 * plane value 0xFF), compute scroll bounds and the initial camera.
 * Call after ck_level_load(); follow with ck_render_level_init(). */
void ck_gameplay_level_start(u8 levelnum);

/* One game frame: input -> thinks -> scrolling -> contacts -> draw
 * pass (msprite) -> Keen/tile interaction. Returns 0 while the level
 * runs, 1 when it ended; g_game.level_finished then holds the
 * CK_LEVEL_END_* result (DIE when Keen died). Lives / game-over policy
 * lives in the flow driver (gameflow.c). */
u8 ck_gameplay_frame(void);

/* CVort_load_level_data tail: scroll bounds from the loaded map header
 * (also used by the world map session). */
void ck_level_setup_bounds(void);

/* g_ck_input (engine pad state) -> ck_input_new (Vorticons GameInput
 * shape); shared by the in-level loop and the world map. */
void ck_build_game_input(void);


#endif
