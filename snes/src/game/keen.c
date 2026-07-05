/* keen.c - SNES transcription of the player states from
 * src/game/enemies.c line 336 onward (ground/jump/pogo/shoot/exit/
 * death) plus the player shot (keengun/zapzot). Think function
 * pointers become CK_THINK_* ids; input comes from ck_input_new/old
 * (built from g_ck_input each frame in gameplay.c).
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/keen.h"
#include "engine/levelload.h"
#include "engine/timer.h"

#define TS g_entities.temp_sprite

void CVort_add_sprite_keengun(s32 pos_x, s32 pos_y)
{
    s16 sprIndex = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[sprIndex];
    u16 tx, ty;
    s->type_ = CK_OBJ_KEENSHOT;
    s->pos_x = pos_x;
    s->pos_y = pos_y + 0x900;
    s->think = CK_THINK_SHOT;
    s->vel_y = 0;
    s->contact = CK_CONTACT_KEENGUN;
    s->frame = CK_SPR_KEENSHOT;
    tx = (u16)CK_W2T(pos_x);
    ty = (u16)CK_W2T(pos_y);
    if (g_game.keen_facing >= 0) {
        s->vel_x = 400;
        if (!TILEINFO_REdge[map_data_tiles[tx + ck_rowofs[ty + 1] + 1]])
            return;
    } else {
        s->vel_x = -400;
        if (!TILEINFO_LEdge[map_data_tiles[ck_rowofs[ty + 1] + tx]])
            return;
    }
    /* fired inside a wall: splash immediately */
    CVort_engine_setCurSound(0x10); /* SNDSHOTHIT */
    s->type_ = CK_OBJ_ZAPZOT;
    s->think = CK_THINK_ZAPZOT;
    s->time = 0;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHR;
    else
        s->frame = CK_SPR_SHOTSPLASHL;
}

extern volatile u8 ck_dbg_stage;
extern volatile u16 ck_dbg_hits;

void CVort_think_keen_ground(void)
{
    s16 tile_standingon_type, currtile_standingon_type, tile_collision;
    ck_dbg_stage = 0x40;
    ck_dbg_hits++;
    s16 map_tile_left, map_tile_right, map_tile_standingon, currX;

    if (TS.varD)
        tile_standingon_type = 1;
    else {
        map_tile_left = CK_W2T(TS.box_x1);
        map_tile_right = CK_W2T(TS.box_x2);
        map_tile_standingon = CK_W2T(TS.box_y2) + 1;
        tile_standingon_type = 1;

        for (currX = map_tile_left; currX <= map_tile_right; currX++) {
            currtile_standingon_type = TILEINFO_UEdge[
                map_data_tiles[ck_rowofs[(u16)map_tile_standingon] + (u16)currX]];
            if (currtile_standingon_type > 1)
                tile_standingon_type = currtile_standingon_type;
        }
    }
    if (ck_input_new.but1jump) {
        TS.varC = TS.vel_x;
        TS.vel_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing >= 0)
            TS.varA = 8;
        else
            TS.varA = 0xE;
        TS.think = CK_THINK_KEEN_JUMP;
    }
    if (tile_standingon_type < 3)
        switch (ck_input_new.direction) {
            case 1:
            case 2:
            case 3:
                CVort_move_left_right(2);
                if (TS.vel_x < 0)
                    ck_input_new.direction = 8;
                break;
            case 5:
            case 6:
            case 7:
                CVort_move_left_right(-2);
                if (TS.vel_x > 0)
                    ck_input_new.direction = 8;
                break;
            default:
                break;
        }
    if ((tile_standingon_type == 1) && (ck_input_new.direction == 8)) {
        if (TS.vel_x > 0) {
            CVort_move_left_right(-3);
            if (TS.vel_x < 0)
                TS.vel_x = 0;
        } else if (TS.vel_x < 0) {
            CVort_move_left_right(3);
            if (TS.vel_x > 0)
                TS.vel_x = 0;
        }
    }
    if (tile_standingon_type == 3) { /* ice */
        if (g_game.keen_facing > 0)
            TS.vel_x = 180;
        else if (g_game.keen_facing < 0)
            TS.vel_x = -180;
    }
    if (!TS.vel_x) {
        if (g_game.keen_facing >= 0)
            TS.frame = 0;
        else
            TS.frame = 4;
    } else {
        if (TS.vel_x > 0) {
            TS.frame = 0;
            if (tile_standingon_type < 3)
                TS.frame += (ck_ticks_lo >> 4) & 3;
        } else {
            TS.frame = 4;
            if (tile_standingon_type < 3)
                TS.frame += (ck_ticks_lo >> 4) & 3;
        }
        g_game.keen_facing = TS.vel_x;
    }
    if (TS.vel_x && ((ck_ticks_lo >> 4) & 1) && (tile_standingon_type < 3)) {
        if ((ck_ticks_lo >> 5) & 1)
            CVort_engine_setCurSound(0x1E);
        else
            CVort_engine_setCurSound(4);
    }
    CVort_do_fall();
    tile_collision = CVort_compute_sprite_delta();
    CVort_check_ceiling();
    if (((tile_collision & 4) || (tile_collision & 1)) && ((ck_ticks_lo >> 4) & 1))
        CVort_engine_setCurSound(5);
    if (!(tile_collision & 2) && !TS.varD) {
        TS.think = CK_THINK_KEEN_JUMP_AIR;
        CVort_engine_setCurSound(0x1B);
        return;
    }
    if (ck_input_new.but1jump) {
        TS.varC = TS.vel_x;
        TS.vel_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing >= 0)
            TS.varA = 8;
        else
            TS.varA = 0xE;
        TS.think = CK_THINK_KEEN_JUMP;
    }
    if (ck_input_new.but2pogo && !ck_input_old.but2pogo) {
        if (g_game.keen_switch) {
            CVort_toggle_switch();
        } else {
            TS.time = 0;
            TS.varB = TS.vel_x;
            TS.vel_x = 0;
            if (keen_gp.stuff[3])
                TS.think = CK_THINK_KEEN_POGO;
        }
    }
    if (ck_input_new.but1jump && ck_input_new.but2pogo && /* two-button firing */
        !ck_input_old.but1jump && !ck_input_old.but2pogo) {
        TS.think = CK_THINK_KEEN_SHOOT;
        TS.del_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing > 0)
            TS.frame = 0x14;
        else
            TS.frame = 0x15;
    }
    if (TS.varD)
        TS.varD--;
}

void CVort_think_keen_jump_ground(void)
{
    TS.frame = TS.varA + TS.time / 6;
    if (ck_input_new.but1jump)
        TS.varB += (s16)ck_sprite_sync * 6;
    else if (TS.time < 12)
        TS.time = 24 - TS.time;

    switch (ck_input_new.direction) {
        case 1: /* right (possibly diagonal) */
        case 2:
        case 3:
            TS.varC += ((s16)ck_sprite_sync << 1);
            if (TS.varC > 0x78)
                TS.varC = 0x78;
            break;
        case 5: /* left (maybe diagonal) */
        case 6:
        case 7:
            TS.varC -= ((s16)ck_sprite_sync << 1);
            if (TS.varC < -0x78)
                TS.varC = -0x78;
            break;
        default:
            break;
    }
    TS.vel_x = 0;
    TS.time += ck_sprite_sync;
    if (TS.time >= 36) {
        TS.think = CK_THINK_KEEN_JUMP_AIR;
        TS.vel_y -= TS.varB;
        TS.vel_x = TS.varC;
        CVort_engine_setCurSound(6);
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
    CVort_check_ceiling();

    if (ck_input_new.but1jump && ck_input_new.but2pogo) { /* two-button firing */
        TS.think = CK_THINK_KEEN_SHOOT;
        TS.del_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing > 0)
            TS.frame = 0x14;
        else
            TS.frame = 0x15;
    }
}

void CVort_think_keen_jump_air(void)
{
    s16 lastDelta;
    switch (ck_input_new.direction) {
        case 1:
        case 2:
        case 3:
            CVort_move_left_right(2);
            if (TS.vel_x < 0)
                ck_input_new.direction = 8;
            break;
        case 5:
        case 6:
        case 7:
            CVort_move_left_right(-2);
            if (TS.vel_x > 0)
                ck_input_new.direction = 8;
            break;
        default:
            break;
    }
    if (ck_input_new.direction == 8) {
        if (TS.vel_x > 0) {
            CVort_move_left_right(-1);
            if (TS.vel_x < 0)
                TS.vel_x = 0;
        } else if (TS.vel_x < 0) {
            CVort_move_left_right(1);
            if (TS.vel_x > 0)
                TS.vel_x = 0;
        }
    }
    if (g_game.keen_facing > 0)
        TS.frame = 0xD;
    else
        TS.frame = 0x13;
    if (TS.vel_x)
        g_game.keen_facing = TS.vel_x;

    CVort_do_fall();
    lastDelta = CVort_compute_sprite_delta();
    if (((lastDelta & 4) || (lastDelta & 1)) && ((ck_ticks_lo >> 4) & 1))
        CVort_engine_setCurSound(5);
    if (lastDelta & 2) {
        TS.think = CK_THINK_KEEN_GROUND;
        CVort_engine_setCurSound(7);
        return;
    }
    if (lastDelta & 8)
        CVort_engine_setCurSound(0x15);

    CVort_check_ceiling();
    if (ck_input_new.but2pogo && !ck_input_old.but2pogo) {
        if (g_game.keen_switch)
            CVort_toggle_switch();
        else if (keen_gp.stuff[3]) /* Keen has gotten a pogo stick? */
            TS.think = CK_THINK_KEEN_POGO_AIR;
    }
    if (ck_input_new.but1jump && ck_input_new.but2pogo && /* two-button firing */
        !ck_input_old.but1jump && !ck_input_old.but2pogo) {
        TS.think = CK_THINK_KEEN_SHOOT;
        TS.del_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing > 0)
            TS.frame = 0x14;
        else
            TS.frame = 0x15;
    }
}

void CVort_think_keen_shoot(void)
{
    TS.time += ck_sprite_sync;
    if (!TS.varB && (TS.time > 1)) {
        if (keen_gp.ammo) {
            CVort_engine_setCurSound(0xC);
            keen_gp.ammo--;
            CVort_add_sprite_keengun(TS.pos_x, TS.pos_y);
        } else
            CVort_engine_setCurSound(0x24);
        TS.varB = 1;
    }
    if ((TS.time > 30) && !ck_input_new.but1jump && !ck_input_new.but2pogo)
        TS.think = CK_THINK_KEEN_GROUND;
    if (TS.vel_x > 0) {
        CVort_move_left_right(-1);
        if (TS.vel_x < 0)
            TS.vel_x = 0;
    } else if (TS.vel_x < 0) {
        CVort_move_left_right(1);
        if (TS.vel_x > 0)
            TS.vel_x = 0;
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
    CVort_check_ceiling();
}

void CVort_think_keen_pogo_air(void)
{
    s16 currDelta;
    /* (the original re-reads the controls here; on SNES ck_input_new
     * is already this frame's state) */
    switch (ck_input_new.direction) {
        case 1:
        case 2:
        case 3:
            CVort_move_left_right(1);
            if (TS.vel_x < 0)
                ck_input_new.direction = 8;
            break;
        case 5:
        case 6:
        case 7:
            CVort_move_left_right(-1);
            if (TS.vel_x > 0)
                ck_input_new.direction = 8;
            break;
        default:
            break;
    }
    if (ck_input_new.but1jump && (TS.vel_y < 0))
        CVort_pogo_jump(200, -1);
    if (g_game.god_mode && ck_input_new.but1jump) /* cheat in effect! */
        TS.vel_y = -200;
    if (TS.vel_x)
        g_game.keen_facing = TS.vel_x;
    if (g_game.keen_facing >= 0)
        TS.varA = 0x18;
    else
        TS.varA = 0x1A;
    TS.frame = TS.varA;
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (((currDelta & 4) || (currDelta & 1)) && ((ck_ticks_lo >> 4) & 1))
        CVort_engine_setCurSound(5);
    if (currDelta & 2) {
        TS.think = CK_THINK_KEEN_POGO;
        TS.time = 0;
        TS.varB = TS.vel_x;
        TS.vel_x = 0;
    }
    if (currDelta & 8)
        CVort_engine_setCurSound(0x15);
    CVort_check_ceiling();
    if (ck_input_new.but2pogo && !ck_input_old.but2pogo) {
        if (g_game.keen_switch)
            CVort_toggle_switch();
        else
            TS.think = CK_THINK_KEEN_JUMP_AIR;
    }
    if (ck_input_new.but1jump && ck_input_new.but2pogo) { /* two-button firing */
        TS.think = CK_THINK_KEEN_SHOOT;
        TS.del_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing > 0)
            TS.frame = 0x14;
        else
            TS.frame = 0x15;
    }
    if (TS.varD)
        TS.varD--;
}

void CVort_think_keen_pogo_ground(void)
{
    TS.del_x = TS.vel_x = 0;
    TS.frame = TS.varA + 1;
    TS.time += ck_sprite_sync;
    if (TS.time > 22) {
        TS.think = CK_THINK_KEEN_POGO_AIR;
        TS.varD = 0;
        TS.vel_y -= 200;
        TS.vel_x = TS.varB;
        CVort_engine_setCurSound(6);
    }
    if (ck_input_new.but2pogo && !ck_input_old.but2pogo)
        TS.think = CK_THINK_KEEN_GROUND;
    if (ck_input_new.but1jump && ck_input_new.but2pogo) { /* two-button firing */
        TS.think = CK_THINK_KEEN_SHOOT;
        TS.del_x = 0;
        TS.varB = 0;
        TS.time = 0;
        if (g_game.keen_facing > 0)
            TS.frame = 0x14;
        else
            TS.frame = 0x15;
    }
    CVort_compute_sprite_delta();
    TS.varD = 0;
}

void CVort_think_keen_exit(void)
{
    /* (door boundary tile redraw omitted until the renderer grows a
     * dirty-tile path - Keen just walks off through the doorway) */
    TS.del_x = (s16)ck_sprite_sync * 60;
    TS.frame = (ck_ticks_lo >> 4) & 3;
    g_game.keen_facing = TS.vel_x;
    if (CK_T2W(TS.time) <= TS.pos_x) {
        TS.type_ = 0;
        g_game.level_finished = CK_LEVEL_END_EXIT;
    }
}

void CVort_think_keen_death(void)
{
    TS.time += ck_sprite_sync;
    if (TS.time >= 200) {
        TS.time = -999;
        TS.vel_x = CVort_get_random() - 0x80;
        TS.vel_y = -0x190;
    }
    TS.frame = ((ck_ticks_lo >> 4) & 1) + 0x16;
    TS.del_x = TS.vel_x * (s16)ck_sprite_sync;
    TS.del_y = TS.vel_y * (s16)ck_sprite_sync;
    if (TS.box_y2 < scroll_y)
        TS.type_ = 0;
}

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE != 1
/* CVort_think_keen_stunned (src/game/enemies.c:756, KEEN2/KEEN3 paths):
 * Keen knocked flat by a Vorticon youth (ep2) / Vortimom (ep3). */
void CVort_think_keen_stunned(void)
{
    TS.frame = (u16)(CK_SPR_KEENSICLE + ((ck_ticks_lo >> 5) & 1));
    TS.time -= (s16)ck_sprite_sync;
    if (TS.time < 0) {
        TS.frame = CK_SPR_KEENGETSUP;
        if (TS.time < -40)
            TS.think = CK_THINK_KEEN_GROUND;
    }

    if (TS.vel_x > 0) {
        CVort_move_left_right(-3);
        if (TS.vel_x < 0)
            TS.vel_x = 0;
    } else if (TS.vel_x < 0) {
        CVort_move_left_right(3);
        if (TS.vel_x > 0)
            TS.vel_x = 0;
    }

    CVort_do_fall();
    CVort_compute_sprite_delta();
    CVort_check_ceiling();
}
#endif

void CVort_think_zapzot(void)
{
    TS.type_ = CK_OBJ_DEAD;
    TS.time += ck_sprite_sync;
    if (TS.time > 20) {
        TS.type_ = CK_OBJ_NULL;
    }
}

void CVort_think_keengun(void)
{
    s16 currDelta = CVort_compute_sprite_delta();
    if (!currDelta)
        return;
    CVort_engine_setCurSound(0x10); /* SNDSHOTHIT */
    TS.type_ = CK_OBJ_ZAPZOT;
    TS.think = CK_THINK_ZAPZOT;
    TS.time = 0;
    if (CVort_get_random() > 0x80)
        TS.frame = CK_SPR_SHOTSPLASHR;
    else
        TS.frame = CK_SPR_SHOTSPLASHL;
}

void CVort_contact_keengun(CkSprite *keengun, CkSprite *contacted)
{
    if ((contacted->type_ == CK_OBJ_KEEN) || (contacted->type_ == CK_OBJ_DEAD))
        return;

    CVort_engine_setCurSound(0x10); /* SNDSHOTHIT */
    keengun->think = CK_THINK_ZAPZOT;
    keengun->contact = CK_CONTACT_NOP;
    keengun->time = 0;
    if (CVort_get_random() > 0x80)
        keengun->frame = CK_SPR_SHOTSPLASHR;
    else
        keengun->frame = CK_SPR_SHOTSPLASHL;
}
