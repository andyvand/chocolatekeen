/* World map session (level 80), transcribed from
 * src/game/worldmap.c (CVort_draw_worldmap map loop, mark_cities_done,
 * place_keen_on_worldmap, check_world_map_col, move_worldmap) and the
 * ep1 map-sprite handler CVort1_worldmap_sprites / handle_secret_city
 * (src/episodes/episode1.c). The outer level-flow loop lives in
 * gameflow.c.
 */
#ifndef CK_SNES_GAME_WORLDMAP_H
#define CK_SNES_GAME_WORLDMAP_H

#include <snes.h>

/* Keen's world map position (world units), kept across level entries
 * (desktop keen_wmap_x_pos / keen_wmap_y_pos). */
extern s32 keen_wmap_x_pos, keen_wmap_y_pos;

/* Once at boot: bind the ep1 teleporter destination table from the EXE
 * image (episode1_engine.c offsets). */
void ck_worldmap_init(void);

/* New game: forget the map position (Keen is re-placed from the map's
 * 0xFF sprite marker) and any pending secret-city teleport. */
void ck_worldmap_new_game(void);

/* Level 6 exited through the secret teleporter tile
 * (CK_LEVEL_END_SECRET): arrive at teleporters[2] on the next map
 * entry (desktop CVort1_handle_secret_city). */
void ck_worldmap_secret_city(void);

/* Session setup. Call with the screen forced blank, after
 * ck_level_load(80) and before ck_render_level_init(): marks done
 * cities, places/restores Keen, computes bounds/scroll/camera. */
void ck_worldmap_enter(void);

/* One map frame (input -> move/collide -> scroll -> draw Keen ->
 * map-sprite triggers). Returns the level number to enter (1..16) or
 * 0 to keep walking. */
u8 ck_worldmap_frame(void);

#endif
