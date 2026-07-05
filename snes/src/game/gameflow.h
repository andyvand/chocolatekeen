/* Game flow driver: the outer loop of CVort_draw_worldmap
 * (src/game/worldmap.c) - world map session <-> level play, lives,
 * the ep1 win trigger and game over. Presentation (menus, dialogs,
 * ending screens) is M6; the win/game-over paths are stubs behind
 * real trigger logic.
 */
#ifndef CK_SNES_GAME_GAMEFLOW_H
#define CK_SNES_GAME_GAMEFLOW_H

#include <snes.h>

/* Live-debug flow markers (read via emulator RAM). */
extern volatile u8 ck_flow_gameover;  /* increments on each game over  */
extern volatile u8 ck_flow_win;       /* set when the ep1 win triggers */

/* Runs the whole game forever: new profile -> world map -> levels ->
 * win/game over -> new profile. Never returns. */
void ck_gameflow_run(void);

#endif
