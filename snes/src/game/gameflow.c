/* gameflow.c - SNES transcription of the outer game loop in
 * src/game/worldmap.c CVort_draw_worldmap:
 *
 *   do {                          // per map visit
 *       (re)load the world map, mark cities done, restore Keen
 *       run the map until Keen enters a level spot
 *       if the level isn't done yet:
 *           play it (CVort_draw_level -> ck_gameplay_frame loop)
 *           DIE    -> lives--, back to the map
 *           EXIT   -> mark level done (autosave slot 0)
 *           SECRET -> secret-city teleport on the next map entry
 *           all 4 ship parts collected -> win
 *   } while (lives > -1);         // then game over
 *
 * Win and game over are stubs (M6 owns the presentation): the win
 * freezes on a distinct backdrop with ck_flow_win set; game over bumps
 * ck_flow_gameover and restarts with a fresh profile on the map.
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/gameplay.h"
#include "game/physics.h"
#include "game/worldmap.h"
#include "game/gameflow.h"
#include "game/episode2.h"
#include "game/episode3.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/timer.h"
#include "engine/save.h"
#include "engine/text.h"
#include "engine/input.h"
#include "game/ui.h"
#include "data_format.h"

volatile u8 ck_flow_gameover;
volatile u8 ck_flow_win;

static u16 s_lastVbl;            /* snes_vblank_count at last game frame */

/* DOS ran its loop with variable frame time and fed the elapsed tick
 * count into the physics (sprite_sync). Feed the timer the number of
 * video frames the previous game frame actually took, so wall-clock
 * game speed is frame-rate independent. Clamped: a long stall (level
 * load hiccup) must not become a physics lurch. */
static u8 flow_elapsed_frames(void)
{
    u16 now = snes_vblank_count;
    u16 d = (u16)(now - s_lastVbl);
    s_lastVbl = now;
    if (d < 1)
        d = 1;
    if (d > 6)
        d = 6;
    return (u8)d;
}

/* One rendered frame tail (matches the M4 main-loop plumbing). */
static void flow_frame_end(void)
{
    ck_render_update();
    WaitForVBlank();
    ck_render_vblank();
    ck_msprite_vblank();
    ck_text_vblank();
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    ck_lights_vblank();          /* queued lights-out palette swap */
#endif
}

static void ck_flow_save(u8 slotNum);   /* defined below */

/* ---- world map session ------------------------------------------------- */
static u8 ck_map_session(void)
{
    const CkLevelEntry *e = ck_level_find(80);
    u8 lvl;

    setScreenOff();
    ck_level_load(80);
    ck_render_tset = e->tset;
    ck_worldmap_enter();
    ck_render_level_init();
    ck_msprite_flush_all();      /* forced blank: drain chr uploads */
    setScreenOn();

    s_lastVbl = snes_vblank_count;
    for (;;) {
        ck_timer_frame(flow_elapsed_frames());
        lvl = ck_worldmap_frame();   /* draws Keen (msprite inside) */
        if (g_ck_input.start_pressed) {
            /* manual save (DOS F5 equivalent): pick a slot */
            u8 pick = ck_ui_slot_picker(1);
            if (pick != 0xFF) {
                ck_flow_save(pick);
                ck_ui_message("GAME SAVED", 0);
            }
            bgSetEnable(0);      /* picker blanks BG1; map is intact */
            s_lastVbl = snes_vblank_count; /* pause time doesn't count */
        }
        flow_frame_end();
        if (lvl)
            return lvl;
    }
}


/* ---- one level (CVort_draw_level shell) --------------------------------- */
static u8 ck_play_level(u8 levelNum)
{
    const CkLevelEntry *e = ck_level_find(levelNum);
    if (!e)
        return CK_LEVEL_END_EXIT;    /* not in ROM: count as completed */

    setScreenOff();
    ck_level_load(levelNum);
    ck_render_tset = e->tset;
    ck_gameplay_level_start(levelNum);
    ck_render_level_init();
    ck_msprite_flush_all();      /* forced blank: drain chr uploads */
    setScreenOn();

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
#endif

    s_lastVbl = snes_vblank_count;
    for (;;) {
        ck_timer_frame(flow_elapsed_frames());
        if (g_ck_input.start_pressed) {
            ck_ui_status_box();  /* gameplay paused while open */
            s_lastVbl = snes_vblank_count; /* pause time doesn't count */
        }
        /* (no outer msprite begin/end: level_draw_sprites inside
         * ck_gameplay_frame brackets its own OAM frame) */
        if (ck_gameplay_frame())
            break;
        flow_frame_end();
    }
    return (u8)g_game.level_finished;
}

/* ---- save serializer (slot 0 = autosave on level completion) ------------ */
static void ck_flow_save(u8 slotNum)
{
    CkSaveSlot s;
    u8 i;
    u16 mask = 0;
    for (i = 0; i < 9; i++)
        s.stuff[i] = keen_gp.stuff[i];
    s.lives = keen_gp.lives;
    s.ammo = keen_gp.ammo;
    s.score = keen_gp.score;
    s.extra_life_pts = keen_gp.extra_life_pts;
    for (i = 0; i < 16; i++) {
        if (keen_gp.levels[i])
            mask |= (u16)(1 << i);
    }
    s.doneLevels = mask;
    mask = 0;
    for (i = 0; i < 8; i++) {
        if (keen_gp.targets[i])
            mask |= (u16)(1 << i);
    }
    s.targetsMask = mask;
    s.worldX = (u16)CK_W2T(keen_wmap_x_pos);
    s.worldY = (u16)CK_W2T(keen_wmap_y_pos);
    s.levelNum = 0;              /* on the world map */
    s.pad = 0;
    ck_save_write(slotNum, &s);
}

static void ck_flow_autosave(void)
{
    ck_flow_save(0);
}

/* ---- endings -------------------------------------------------------------- */
static void ck_win_show(void)
{
    ck_flow_win = 1;
    ck_msprite_begin();
    ck_msprite_end();            /* hide all sprites */
    ck_ui_text_viewer(ck_text_end);
}

/* Continue: restore the profile + map position from a save slot. */
static u8 ck_flow_load(u8 slot)
{
    CkSaveSlot s;
    u8 i;
    if (!ck_save_read(slot, &s))
        return 0;
    for (i = 0; i < 9; i++)
        keen_gp.stuff[i] = s.stuff[i];
    keen_gp.lives = s.lives;
    keen_gp.ammo = s.ammo;
    keen_gp.score = s.score;
    keen_gp.extra_life_pts = s.extra_life_pts;
    for (i = 0; i < 16; i++)
        keen_gp.levels[i] = (u16)((s.doneLevels >> i) & 1);
    for (i = 0; i < 8; i++)
        keen_gp.targets[i] = (u16)((s.targetsMask >> i) & 1);
    keen_wmap_x_pos = CK_T2W(s.worldX);
    keen_wmap_y_pos = CK_T2W(s.worldY);
    return 1;
}

/* ---- the outer loop ------------------------------------------------------ */
void ck_gameflow_run(void)
{
    u8 lvl, rc;

    ck_flow_gameover = 0;
    ck_flow_win = 0;
    ck_worldmap_init();

    for (;;) {                   /* one full game per iteration */
        u8 choice;

        ck_ui_titlescreen();
        for (;;) {
            choice = ck_ui_menu();
            if (choice == CK_MENU_STORY) {
                ck_ui_text_viewer(ck_text_story);
                continue;
            }
            if (choice == CK_MENU_BACK)
                continue;        /* slot picker cancelled */
            break;
        }

        ck_profile_new();
        ck_worldmap_new_game();
        if (choice == CK_MENU_CONTINUE)
            ck_flow_load(ck_ui_menu_slot);

        do {
            lvl = ck_map_session();
            if (keen_gp.levels[lvl - 1])
                continue;        /* already done (marker not cleared) */

            CVort_engine_setCurSound(3); /* enter-level sound */
            rc = ck_play_level(lvl);

            if (rc == CK_LEVEL_END_DIE) {
                keen_gp.lives--;
                if (keen_gp.lives > -1)
                    ck_ui_keens_left();
                continue;        /* back to the map (or game over) */
            }
            if (rc == CK_LEVEL_END_EXIT) {
                keen_gp.levels[lvl - 1] = 1;
                ck_flow_autosave();
            } else if (rc == CK_LEVEL_END_SECRET) {
                ck_worldmap_secret_city();
            }
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
            if (rc == CK_LEVEL_END_TANTALUS) {
                /* Keen pulled the tantalus switch: the earth explodes
                 * and the game is over (worldmap.c LEVEL_END_TANTALUS
                 * path). */
                ck_ui_message("OOPS.", 0);
                ck_ep2_earth_explode();
                keen_gp.lives = -1;
                break;
            }
#endif

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
            /* ep1 win: joystick, battery, vacuum, everclear all
             * aboard (worldmap.c GAMEVER_KEEN1 check; nested ifs for
             * 816-tcc) */
            if (keen_gp.stuff[0])
                if (keen_gp.stuff[4])
                    if (keen_gp.stuff[1])
                        if (keen_gp.stuff[2]) {
                            ck_win_show();
                            break; /* back to the title */
                        }
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
            /* ep2 win: all eight tantalus rays disabled (cities saved) */
            {
                u8 li, totalTargets = 0;
                for (li = 0; li < 8; li++)
                    totalTargets += (u8)keen_gp.targets[li];
                if (totalTargets == 8) {
                    ck_win_show();
                    break; /* back to the title */
                }
            }
#else
            /* ep3 win: the Mangling Machine level (16) completed */
            if (lvl == 16) {
                if (rc == CK_LEVEL_END_EXIT) {
                    ck_win_show();
                    break; /* back to the title */
                }
            }
#endif
        } while (keen_gp.lives > -1);

        if (keen_gp.lives <= -1) {
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
            /* ep2 game over: earth's fate is sealed either way */
            if (rc != CK_LEVEL_END_TANTALUS)
                ck_ep2_earth_explode();
#endif
            ck_flow_gameover++;
            ck_ui_game_over();
        }
    }
}
