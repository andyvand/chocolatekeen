/* episode1.c - SNES transcription of src/episodes/episode1.c (sprite
 * spawns, enemy thinks/contacts, contact_keen, ice cannon / shot chain
 * bodies) and the ep1-relevant parts of src/game/enemies.c (Vorticon,
 * tank shot). Behavior-preserving; function pointers -> u8 ids.
 *
 * 816-tcc rules observed: no negated short-circuit ORs, no zero-init
 * assumptions (sprites/bodies are zeroed by CVort_add_sprite/_body),
 * positions s32, everything else s16/u16.
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/keen.h"
#include "game/gameplay.h"
#include "game/episode1.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/timer.h"

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1

#define TS g_entities.temp_sprite

#define CK_SPR_TANKSHOT (CK_SPR_KEENSHOT + 1) /* 109: CVort1_spr_tankshot */
#define CK_OBJ_ENEMYSHOT 11
#define CK_OBJ_VORTICON  4

/* ---- spawn functions (CVort1_add_sprite_*) ---------------------------- */

static void CVort1_add_sprite_yorp(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 2;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY) + 0x800;
    s->think = CK_THINK_YORP_WALK;
    if (s->pos_x < g_entities.sprites[0].pos_x)
        s->vel_x = 60;
    else
        s->vel_x = -60;
    s->contact = CK_CONTACT_YORP;
    s->frame = 0x30;
}

static void CVort1_add_sprite_garg(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 3;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_GARG_LOOK;
    s->contact = CK_CONTACT_GARG;
    s->frame = 0x3C;
}

static void CVort_add_sprite_vorticon(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = CK_OBJ_VORTICON;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_VORT_WALK;
    s->contact = CK_CONTACT_VORTICON;
    s->health = 3;
    if (g_game.current_level == 16)
        s->health = 104;
    if (s->pos_x > g_entities.sprites[0].pos_x)
        s->vel_x = -90;
    else
        s->vel_x = 90;
    s->frame = 0x4E; /* vortstand1 */
}

static void CVort1_add_sprite_butler(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 5;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->vel_x = 90;
    if (s->pos_x < g_entities.sprites[0].pos_x)
        s->vel_x = -s->vel_x;
    s->think = CK_THINK_BUTLER_WALK;
    s->contact = CK_CONTACT_BUTLER;
    s->frame = 0x60;
}

static void CVort1_add_sprite_tankbot(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 6;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY) + 0x800;
    s->vel_x = 90;
    if (s->pos_x < g_entities.sprites[0].pos_x)
        s->vel_x = -s->vel_x;
    s->think = CK_THINK_TANK_SPAWN;
    s->contact = CK_CONTACT_NOP;
    s->frame = 0x6A;
}

static void CVort1_add_sprite_chain(u16 tileX, u16 tileY)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 8;
    s->pos_x = CK_T2W(tileX);
    s->pos_y = CK_T2W(tileY);
    s->think = CK_THINK_NOP; /* static sprite */
    s->contact = CK_CONTACT_CHAIN;
    s->frame = 0x72;
}

static void CVort1_add_body_cannon(u16 tileX, u16 tileY, s16 variant)
{
    s16 i = CVort_add_body();
    CkBody *b = &g_entities.bodies[i];
    b->type_ = 3;
    b->think = CK_BODY_ICE_CANNON;
    b->variant = variant;
    b->tile_x = tileX;
    b->tile_y = tileY;
}

static void CVort1_add_sprite_cube(s32 pos_x, s32 pos_y, s16 cannon_type)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    s->type_ = 15;
    s->pos_x = pos_x;
    s->pos_y = pos_y;
    s->think = CK_THINK_CUBE;
    switch (cannon_type) {
        case 0:
            s->pos_x += 0x1000;
            s->vel_y = -200;
            s->vel_x = 200;
            break;
        case 1:
            s->vel_y = -200;
            s->vel_x = 0;
            break;
        case 2:
            s->vel_y = 200;
            s->vel_x = 0;
            break;
        case 3:
            s->vel_y = -200;
            s->vel_x = -200;
            break;
        default:
            break;
    }
    s->contact = CK_CONTACT_CUBE;
    s->frame = 0x70;
}

static void CVort_add_sprite_tankshot(s32 pos_x, s32 pos_y, s16 vel_x)
{
    s16 i = CVort_add_sprite();
    CkSprite *s = &g_entities.sprites[i];
    u16 tx, ty;
    s->type_ = CK_OBJ_ENEMYSHOT;
    s->pos_x = pos_x;
    s->pos_y = pos_y + 0x500;
    s->think = CK_THINK_SHOT;
    s->vel_x = vel_x;
    s->contact = CK_CONTACT_TANKSHOT;
    s->frame = CK_SPR_TANKSHOT;
    tx = (u16)CK_W2T(pos_x);
    ty = (u16)CK_W2T(pos_y);
    if (vel_x >= 0) {
        if (!TILEINFO_REdge[map_data_tiles[tx + ck_rowofs[ty + 1] + 1]])
            return;
    } else {
        if (!TILEINFO_LEdge[map_data_tiles[ck_rowofs[ty + 1] + tx]])
            return;
    }
    CVort_engine_setCurSound(0x25); /* SNDSHOTHIT (0x25=37: snd_shothit) */
    s->type_ = CK_OBJ_ZAPZOT;
    s->think = CK_THINK_ZAPZOT;
    s->time = 0;
    if (CVort_get_random() > 0x80)
        s->frame = CK_SPR_SHOTSPLASHL;
    else
        s->frame = CK_SPR_SHOTSPLASHR;
}

/* CVort1_init_level's per-cell spawn dispatch (the value->type table). */
void CVort1_spawn_plane_value(u16 currSprite, u16 tileX, u16 tileY)
{
    switch (currSprite) {
        case 1:
            CVort1_add_sprite_yorp(tileX, tileY);
            break;
        case 2:
            CVort1_add_sprite_garg(tileX, tileY);
            break;
        case 3:
            CVort_add_sprite_vorticon(tileX, tileY);
            break;
        case 4:
            CVort1_add_sprite_butler(tileX, tileY);
            break;
        case 5:
            CVort1_add_sprite_tankbot(tileX, tileY);
            break;
        case 6:
            CVort1_add_body_cannon(tileX, tileY, 0);
            break;
        case 7:
            CVort1_add_body_cannon(tileX, tileY, 1);
            break;
        case 8:
            CVort1_add_body_cannon(tileX, tileY, 2);
            break;
        case 9:
            CVort1_add_body_cannon(tileX, tileY, 3);
            break;
        case 0xA:
            CVort1_add_sprite_chain(tileX, tileY);
            break;
        case 0xFF: /* Keen */
            g_entities.sprites[0].pos_x = CK_T2W(tileX);
            g_entities.sprites[0].pos_y = CK_T2W(tileY) + 0x800;
            break;
        default:
            break;
    }
}

/* ---- contact_keen ------------------------------------------------------ */

void CVort1_contact_keen(CkSprite *keen, CkSprite *contacted)
{
    switch (contacted->type_) {
        case 2: /* Yorp */
            if (contacted->think == CK_THINK_YORP_STUNNED)
                return;
            /* currently the Yorp is not stunned */
            if ((keen->vel_y <= 0) ||
                (keen->pos_y + 0x800 > contacted->pos_y)) { /* Yorp pushes */
                keen->vel_y = 0;
                if (contacted->vel_x > 0) /* pushes right */
                    keen->vel_x = 0xF0;
                else
                    keen->vel_x = -0xF0;
                CVort_engine_setCurSound(0x1D);
                return;
            }
            /* Yorp gets stunned (Keen bonks it from above) */
            contacted->think = CK_THINK_YORP_STUNNED;
            contacted->time = 0;
            keen->vel_y = 0;
            keen->think = CK_THINK_KEEN_GROUND;
            CVort_engine_setCurSound(0x1F);
            return;
        case 5: /* push Keen (Butler / Tank) */
        case 6:
            keen->vel_y = 0;
            if (contacted->vel_x > 0)
                keen->vel_x = 240;
            else
                keen->vel_x = -240;
            CVort_engine_setCurSound(0x1D);
            return;
        case 3: /* kill Keen (Garg / Vorticon / enemy shot) */
        case 4:
        case 11:
            if ((contacted->type_ == 11) &&
                (keen->think == CK_THINK_KEEN_FROZEN))
                keen->time = 0;
            else
                CVort_kill_keen();
            return;
        case 15: /* ice cube: freeze Keen */
            keen->think = CK_THINK_KEEN_FROZEN;
            keen->vel_x = contacted->vel_x;
            keen->vel_y = contacted->vel_y;
            keen->time = 800;
            CVort_engine_setCurSound(0x28);
            return;
        default:
            return;
    }
}

void CVort1_think_keen_frozen(void)
{
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + 0x1C);
    TS.time -= (s16)ck_sprite_sync;
    if (TS.time < 0) {
        TS.frame = 0x1F;
        if (TS.time < -50)
            TS.think = CK_THINK_KEEN_GROUND;
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
    CVort_check_ceiling();
}

/* ---- Yorp -------------------------------------------------------------- */

void CVort1_think_yorp_walk(void)
{
    s16 currDelta;
    if (!TS.vel_y) {
        if ((u16)CVort_get_random() < ck_sprite_sync)
            TS.vel_y = -CVort_calc_jump_height(0x80);
        else if ((u16)CVort_get_random() < ck_sprite_sync) {
            TS.think = CK_THINK_YORP_LOOK;
            TS.time = 0;
        }
    }
    if (TS.vel_x > 0)
        TS.frame = 0x34;
    else
        TS.frame = 0x36;
    TS.frame += (ck_ticks_lo >> 4) & 1;
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 4)
        TS.vel_x = -0x3C;
    if (currDelta & 1)
        TS.vel_x = 0x3C;
}

void CVort1_think_yorp_look(void)
{
    TS.vel_x = 0;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 3) + 0x30);
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 200) {
        if (TS.pos_x < g_entities.sprites[0].pos_x)
            TS.vel_x = 0x3C;
        else
            TS.vel_x = -0x3C;
        TS.think = CK_THINK_YORP_WALK;
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort1_think_yorp_stunned(void)
{
    TS.frame = (u16)(((ck_ticks_lo >> 4) & 1) + 0x38);
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 800) {
        TS.time = 0;
        TS.think = CK_THINK_YORP_LOOK;
    }
    TS.vel_x = 0;
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort1_contact_yorp(CkSprite *yorp, CkSprite *contacted)
{
    /* separate ifs (816-tcc: no negated short-circuit chains) */
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    yorp->time = 0;
    yorp->varB = 2;
    yorp->frame = 0x3A;
    yorp->think = CK_THINK_KILL_SPRITE;
    yorp->contact = CK_CONTACT_NOP;
    yorp->vel_y = -80;
    CVort_engine_setCurSound(0x22);
}

/* ---- Garg -------------------------------------------------------------- */

void CVort1_think_garg_move(void)
{
    s16 currDelta;
    if (!TS.vel_y && (TS.vel_x > -220) && (TS.vel_x < 220)) {
        if (CVort_get_random() < (s16)ck_sprite_sync) {
            TS.think = CK_THINK_GARG_LOOK;
            TS.time = 0;
        }
    }
    if (TS.vel_x > 0)
        TS.frame = 0x40;
    else
        TS.frame = 0x42;
    TS.frame += (ck_ticks_lo >> 4) & 1;
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 4)
        TS.vel_x = -60;
    if (currDelta & 1)
        TS.vel_x = 60;
    if (currDelta & 2)
        TS.time = 0;
    else if (((TS.vel_x == 220) || (TS.vel_x == -220)) && !TS.time) {
        TS.time = 1;
        TS.vel_y = -200;
    }
}

void CVort1_think_garg_look(void)
{
    TS.vel_x = 0;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 3) + 0x3C);
    TS.time += (s16)ck_sprite_sync;
    CVort_do_fall();
    CVort_compute_sprite_delta();
    if (TS.time < 80)
        return;
    if (TS.pos_y + 0x800 == g_entities.sprites[0].pos_y)
        TS.vel_x = 220; /* charge: Keen is on the same level */
    else
        TS.vel_x = 60;
    if (TS.pos_x > g_entities.sprites[0].pos_x)
        TS.vel_x = -TS.vel_x;
    TS.think = CK_THINK_GARG_MOVE;
}

void CVort1_contact_garg(CkSprite *garg, CkSprite *contacted)
{
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    garg->time = 0;
    garg->varB = 2;
    garg->frame = 0x44;
    garg->think = CK_THINK_KILL_SPRITE;
    garg->contact = CK_CONTACT_NOP;
    garg->vel_y = -80;
    CVort_engine_setCurSound(0x23);
}

/* ---- Butler bot --------------------------------------------------------- */

void CVort1_think_butler_walk(void)
{
    s16 currDelta;
    if (TS.vel_x > 0)
        TS.frame = 0x58;
    else
        TS.frame = 0x5C;
    TS.frame += (ck_ticks_lo >> 5) & 3;
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (!(currDelta & 2)) { /* don't fall down! */
        TS.del_x = (s16)((-TS.del_x) << 1);
        TS.del_y = 0;
        TS.varB = -TS.vel_x;
        TS.think = CK_THINK_BUTLER_TURN;
        TS.vel_x = 0;
        TS.time = 0;
    }
    if (currDelta & 4) {
        TS.varB = -90;
        TS.think = CK_THINK_BUTLER_TURN;
        TS.vel_x = 0;
        TS.time = 0;
    }
    if (currDelta & 1) {
        TS.varB = 90;
        TS.think = CK_THINK_BUTLER_TURN;
        TS.vel_x = 0;
        TS.time = 0;
    }
}

void CVort1_think_butler_turn(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (TS.time > 50) {
        TS.think = CK_THINK_BUTLER_WALK;
        TS.vel_x = TS.varB;
    }
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 1) + 0x60);
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort1_contact_butler(CkSprite *butler, CkSprite *contacted)
{
    if (contacted->type_ != 10)
        return;
    butler->time = 0;
}

/* ---- Tank bot ------------------------------------------------------------ */

void CVort1_think_tankbot_move(void)
{
    s16 currDelta;
    if (TS.vel_x > 0)
        TS.frame = 0x62;
    else
        TS.frame = 0x66;
    TS.frame += (ck_ticks_lo >> 3) & 3;
    if (CVort_get_random() < (s16)ck_sprite_sync) {
        TS.think = CK_THINK_TANK_SHOOT;
        TS.time = 0;
        TS.varB = 0;
        TS.varC = TS.vel_x;
        TS.vel_x = 0;
    }
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    TS.vel_y = 0;
    TS.del_y = 0;
    if (!(currDelta & 2)) {
        if (TS.frame < 0x66) {
            TS.del_x = (s16)ck_sprite_sync * (-180);
            TS.varB = -90;
        } else {
            TS.del_x = (s16)ck_sprite_sync * 180;
            TS.varB = 90;
        }
    } else {
        if (currDelta & 4) {
            TS.varB = -90;
            TS.think = CK_THINK_TANK_TURN;
            TS.vel_x = 0;
            TS.time = 0;
        }
        if (!(currDelta & 1))
            return;
        TS.varB = 90;
    }
    TS.think = CK_THINK_TANK_TURN;
    TS.vel_x = 0;
    TS.time = 0;
}

void CVort1_think_tankbot_spawn(void)
{
    CVort_do_fall();
    if (CVort_compute_sprite_delta() & 2)
        TS.think = CK_THINK_TANK_MOVE;
}

void CVort1_think_tankbot_turn(void)
{
    TS.time += (s16)ck_sprite_sync;
    TS.frame = (u16)(((ck_ticks_lo >> 4) & 1) + 0x6A);
    if (TS.time > 50) {
        TS.think = CK_THINK_TANK_MOVE;
        TS.vel_x = TS.varB;
    }
}

void CVort1_think_tankbot_shoot(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (!TS.varB && (TS.time > 80)) {
        CVort_engine_setCurSound(0x26); /* snd_tankfire */
        if (TS.varC < 0)
            CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y, -350);
        else
            CVort_add_sprite_tankshot(TS.pos_x, TS.pos_y, 350);
        TS.varB = 1;
    }
    if (TS.time <= 120)
        return;
    TS.varB = 90;
    if (TS.pos_x > g_entities.sprites[0].pos_x)
        TS.varB = -90;
    TS.time = 0;
    TS.think = CK_THINK_TANK_TURN;
}

void CVort_contact_tankshot(CkSprite *tankshot, CkSprite *contacted)
{
    if (contacted->type_ == 6)
        return;
    if (contacted->type_ == 14)
        return;
    CVort_engine_setCurSound(0x25);
    tankshot->think = CK_THINK_ZAPZOT;
    tankshot->time = 0;
    tankshot->contact = CK_CONTACT_NOP;
    if (CVort_get_random() > 0x80)
        tankshot->frame = CK_SPR_SHOTSPLASHR;
    else
        tankshot->frame = CK_SPR_SHOTSPLASHL;
}

/* ---- Vorticon (shared, src/game/enemies.c, GAMEVER_KEEN1 paths) -------- */

void CVort_think_vorticon_walk(void)
{
    s16 currDelta;
    if (TS.vel_x > 0)
        TS.frame = 0x4A; /* vortright1 */
    else
        TS.frame = 0x46; /* vortleft1 */
    TS.frame += (ck_ticks_lo >> 4) & 3;
    if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
        TS.vel_y = -CVort_calc_jump_height(300);
        TS.think = CK_THINK_VORT_JUMP;
    } else if (CVort_get_random() < (s16)(ck_sprite_sync << 1)) {
        if (TS.vel_x == 90)
            TS.vel_x = 120;
        else if (TS.vel_x > 90) {
            if (TS.vel_x == 120)
                TS.vel_x = 90;
        } else if (TS.vel_x == -120)
            TS.vel_x = -90;
        else if (TS.vel_x == -90)
            TS.vel_x = -120;
    }
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 4)
        TS.vel_x = -90;
    if (currDelta & 1)
        TS.vel_x = 90;
}

void CVort_think_vorticon_jump(void)
{
    s16 currDelta;
    if (TS.vel_x > 0)
        TS.frame = 0x50; /* vortjumpl (sic - vanilla swaps them) */
    else
        TS.frame = 0x51; /* vortjumpr */
    CVort_do_fall();
    currDelta = CVort_compute_sprite_delta();
    if (currDelta & 2) {
        TS.think = CK_THINK_VORT_SEARCH;
        TS.time = 0;
    }
    if (currDelta & 4)
        TS.vel_x = -90;
    if (currDelta & 1)
        TS.vel_x = 90;
}

void CVort_think_vorticon_search(void)
{
    TS.vel_x = 0;
    TS.frame = (u16)(((ck_ticks_lo >> 5) & 3) + 0x4E);
    TS.time += (s16)ck_sprite_sync;
    if (TS.time >= 80) {
        TS.vel_x = 90;
        if (TS.pos_x > g_entities.sprites[0].pos_x)
            TS.vel_x = -TS.vel_x;
        TS.think = CK_THINK_VORT_WALK;
    }
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort_contact_vorticon(CkSprite *vorticon, CkSprite *contacted)
{
    s16 currHealth;
    if (contacted->type_ != 10) {
        if (contacted->type_ != 11)
            return;
    }
    /* GAMEVER_KEEN1 path: post-decrement pattern of the original */
    currHealth = vorticon->health;
    vorticon->health--;
    if (currHealth)
        return;
    CVort_engine_setCurSound(0x27);
    vorticon->time = 0;
    vorticon->varB = 6;
    vorticon->frame = 0x52; /* vortdie1 */
    vorticon->contact = CK_CONTACT_NOP;
    vorticon->think = CK_THINK_KILL_SPRITE;
    /* Desktop adds a border-flash body here; the SNES has no EGA border
     * color, so the flash is skipped. */
}

/* ---- ice cannon / cube / cubettes -------------------------------------- */

void CVort1_body_ice_cannon(CkBody *cannon)
{
    s16 tx = (s16)cannon->tile_x;
    s16 ty = (s16)cannon->tile_y;
    if ((s16)(ck_ticks_lo >> 7) == cannon->field_C)
        return;
    cannon->field_C = (s16)(ck_ticks_lo >> 7);
    if (scroll_x_tile - 4 >= tx)   /* ENGINE_SPRITE_MARGIN_CANNON */
        return;
    if (scroll_y_tile - 4 >= ty)
        return;
    if (scroll_x_tile + 24 <= tx)  /* ENGINE_VIEWPORT_CANNON_EXTRA_X */
        return;
    if (scroll_y_tile + 14 <= ty)  /* ENGINE_VIEWPORT_CANNON_EXTRA_Y */
        return;
    CVort_engine_setCurSound(0x17);
    CVort1_add_sprite_cube(CK_T2W(tx), CK_T2W(ty), cannon->variant);
}

void CVort1_think_cubette_flight(void)
{
    TS.time += (s16)ck_sprite_sync;
    if (TS.time > 60) {
        TS.type_ = 0;
        return;
    }
    CVort_do_fall();
    TS.del_x += TS.vel_x * (s16)ck_sprite_sync;
    TS.del_y += TS.vel_y * (s16)ck_sprite_sync;
}

void CVort1_think_cube(void)
{
    static const s16 cubetteVelX[4] = { 300, 300, -300, -300 };
    static const s16 cubetteVelY[4] = { 300, -300, 300, -300 };
    u8 k;
    s32 px, py;
    if (!CVort_compute_sprite_delta())
        return;
    /* Hit a blocking tile: split into four cubettes. */
    TS.type_ = 0;
    px = TS.pos_x;
    py = TS.pos_y;
    for (k = 0; k < 4; k++) {
        s16 i = CVort_add_sprite();
        CkSprite *s = &g_entities.sprites[i];
        s->type_ = 12;
        s->think = CK_THINK_CUBETTE;
        s->contact = CK_CONTACT_NOP;
        s->pos_x = px;
        s->pos_y = py;
        s->vel_x = cubetteVelX[k];
        s->vel_y = cubetteVelY[k];
        s->frame = 0x71;
    }
    CVort_engine_setCurSound(0x13);
}

void CVort1_contact_cube(CkSprite *cube, CkSprite *contacted)
{
    if (contacted->type_ != CK_OBJ_KEEN) /* only Keen gets frozen */
        return;
    cube->think = CK_THINK_REMOVE_SPRITE;
}

/* ---- chain (level 16 Vorticon Commander machine) ------------------------ */

void CVort1_contact_chain(CkSprite *chain, CkSprite *contacted)
{
    s16 bodyIndex, vorticonIndex;
    if (contacted->type_ != 10)
        return;
    /* the chain itself turns into a zapzot */
    chain->think = CK_THINK_ZAPZOT;
    chain->time = 0;
    chain->contact = CK_CONTACT_NOP;
    if (CVort_get_random() > 0x80)
        chain->frame = CK_SPR_SHOTSPLASHR;
    else
        chain->frame = CK_SPR_SHOTSPLASHL;
    bodyIndex = CVort_add_body();
    g_entities.bodies[bodyIndex].type_ = 4;
    g_entities.bodies[bodyIndex].think = CK_BODY_SHOT_CHAIN;
    g_entities.bodies[bodyIndex].tile_x = CK_W2T(chain->pos_x);
    g_entities.bodies[bodyIndex].tile_y = CK_W2T(chain->pos_y) + 1;
    g_entities.bodies[bodyIndex].variant = 0;
    /* Vanilla scans backwards for the Vorticon; guard against the
     * original's unbounded loop (crash if none present). */
    for (vorticonIndex = g_entities.num_sprites; vorticonIndex >= 0;
         vorticonIndex--) {
        if (g_entities.sprites[vorticonIndex].type_ == CK_OBJ_VORTICON)
            break;
    }
    CVort_engine_setCurSound(0x27); /* snd_vortscream */
    if (vorticonIndex < 0)
        return;
    /* kill the Vorticon */
    g_entities.sprites[vorticonIndex].time = 0;
    g_entities.sprites[vorticonIndex].varB = 6;
    g_entities.sprites[vorticonIndex].frame = 0x52;
    g_entities.sprites[vorticonIndex].contact = CK_CONTACT_NOP;
    g_entities.sprites[vorticonIndex].think = CK_THINK_KILL_SPRITE;
}

void CVort1_body_shot_chain(CkBody *chain)
{
    u16 tx = (u16)chain->tile_x;
    u16 ty = (u16)chain->tile_y;
    u16 x;
    if ((s16)(ck_ticks_lo >> 5) == chain->field_C)
        return;
    chain->field_C = (s16)(ck_ticks_lo >> 5);
    x = (u16)(tx - 4);
    ck_map_set(x, ty, 0x9B);
    ck_map_set(x, (u16)(ty + 1), 0x1DC);
    ck_map_set(x, (u16)(ty + 2), 0x1E9);
    for (x = (u16)(tx - 3); x <= tx + 3; x++) {
        ck_map_set(x, ty, 0x9B);
        ck_map_set(x, (u16)(ty + 1), 0x1DD);
        ck_map_set(x, (u16)(ty + 2), 0x1EA);
    }
    x = (u16)(tx + 4);
    ck_map_set(x, ty, 0x9B);
    ck_map_set(x, (u16)(ty + 1), 0x1DE);
    ck_map_set(x, (u16)(ty + 2), 0x1EB);

    chain->tile_y++;
    chain->variant++;
    if (chain->variant == 4) /* time to finish this */
        chain->type_ = 0;
}

#endif /* CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1 */
