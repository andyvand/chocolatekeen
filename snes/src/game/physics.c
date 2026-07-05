/* physics.c - SNES transcription of src/game/physics.c (player motion,
 * tile collision, camera follow) plus the body thinks (slide door,
 * bridges) and CVort_toggle_switch (src/game/ui.c).
 *
 * 816-tcc notes: positions/boxes stay s32 (original fidelity),
 * velocities/timers s16. Tile coordinates are shifted down to u16
 * before any map indexing; map rows are addressed through ck_rowofs[]
 * (no 32-bit indexing, no divides - %0x1000 becomes &0xFFF on the low
 * word).
 */
#include "game/game_state.h"
#include "game/physics.h"
#include "game/keen.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/timer.h"
#include "data_format.h"
#include "snes_data_gen.h"

#define TS g_entities.temp_sprite

/* ---- bodies (map-mutating helpers go through ck_map_set so the
 * renderer patches the on-screen tilemap) ------------------------------ */

/* CVort_body_slide_door: the desktop body redraws the door as an overlay
 * sliding down behind the floor over 160 ticks. The SNES has no overlay
 * tile layer, so the same 160-tick slide is approximated in map space:
 * halfway the top tile clears and the door art moves down one row, at
 * the end both rows clear. Collision opens up when the tiles clear,
 * which also replaces the desktop's instant map clear in open_door.
 * Body fields: tile_x/tile_y = door top (tile coords), variant = timer,
 * field_10 = door top tile id, field_C = last applied stage. */

/* ---- split-u16 32-bit helpers ----------------------------------------
 * 816-tcc compiles s32 += s16 with a 15-iteration sign-extension loop,
 * s32 >> 12 with a 12-iteration shift loop, and s32 compares with stack
 * spill storms (measured: ~11 scanlines for the plain-s32 hitbox body).
 * These do the same math on the two u16 halves. World coordinates and
 * boxes are non-negative and < 2^24 (solid level borders keep sprites
 * inside), so logical shifts and unsigned compares are exact. */

/* f (s32 lvalue) += o (s16) */
#define CK_ADD16(f, o) do { \
    u16 _lo = ((u16 *)&(f))[0], _o = (u16)(o); \
    u16 _nlo = (u16)(_lo + _o); \
    ((u16 *)&(f))[0] = _nlo; \
    if (_o & 0x8000) \
        ((u16 *)&(f))[1]--; \
    if (_nlo < _lo) \
        ((u16 *)&(f))[1]++; \
} while (0)

/* tile index (world >> 12) of an s32 lvalue */
#define CK_TILE(f) \
    ((u16)((((u16 *)&(f))[0] >> 12) | (((u16 *)&(f))[1] << 4)))

/* tile index of (*f + o) without modifying *f */
static u16 ck_tile_ofs(s32 *f, s16 o)
{
    u16 lo = ((u16 *)f)[0];
    u16 hi = ((u16 *)f)[1];
    u16 uo = (u16)o;
    u16 nlo = (u16)(lo + uo);
    if (uo & 0x8000)
        hi--;
    if (nlo < lo)
        hi++;
    return (u16)((nlo >> 12) | (hi << 4));
}

/* 1 if *a < *b (both non-negative s32) */
static u8 ck_lt32(const s32 *a, const s32 *b)
{
    u16 ah = ((const u16 *)a)[1], bh = ((const u16 *)b)[1];
    if (ah < bh)
        return 1;
    if (ah > bh)
        return 0;
    if (((const u16 *)a)[0] < ((const u16 *)b)[0])
        return 1;
    return 0;
}

void CVort_body_slide_door(CkBody *door)
{
    s16 scaledTime, stage;
    u16 tx = (u16)door->tile_x, ty = (u16)door->tile_y;
    door->variant += (s16)ck_sprite_sync;
    scaledTime = door->variant / 5;
    if (scaledTime > 32)
        scaledTime = 32;
    stage = scaledTime >> 4; /* 0,1,2 */
    if (stage != door->field_C) {
        door->field_C = stage;
        if (stage == 1) {
            ck_map_set(tx, ty, 0x8F);
            ck_map_set(tx, (u16)(ty + 1), (u16)door->field_10);
        } else if (stage >= 2) {
            ck_map_set(tx, ty, 0x8F);
            ck_map_set(tx, (u16)(ty + 1), 0x8F);
        }
    }
    if (scaledTime == 32)
        door->type_ = 0;
}

void CVort_open_door(s16 tileX, s16 tileY)
{
    s16 doorHeight, tileType0, i;
    CVort_engine_setCurSound(0x21);
    /* where is the door located, relative to the touched tile? */
    tileType0 = TILEINFO_Type[
        map_data_tiles[ck_rowofs[(u16)tileY] + (u16)tileX]];
    if (tileType0)
        doorHeight = tileY;
    else
        doorHeight = tileY - 1;
    i = CVort_add_body();
    g_entities.bodies[i].type_ = 1;
    g_entities.bodies[i].think = CK_BODY_SLIDE_DOOR;
    g_entities.bodies[i].tile_x = tileX;      /* tile coords (see above) */
    g_entities.bodies[i].tile_y = doorHeight;
    g_entities.bodies[i].variant = 0;
    g_entities.bodies[i].field_C = 0;
    g_entities.bodies[i].field_10 =
        (s16)map_data_tiles[ck_rowofs[(u16)doorHeight] + (u16)tileX];
    /* consume the key (desktop quirk kept: uses the touched tile type) */
    keen_gp.stuff[3 + tileType0] = 0;
    /* map tiles are cleared by the body as the door slides */
}

void CVort_body_bridge_extend(CkBody *bridge)
{
    s16 currActualX;
    bridge->variant += (s16)ck_sprite_sync;
    if (bridge->variant < 12)
        return;
    bridge->variant -= 12;
    currActualX = (s16)bridge->tile_x + bridge->field_10;
    if (map_data_tiles[ck_rowofs[(u16)bridge->tile_y] + (u16)currActualX]
        != (u16)bridge->field_E) {
        bridge->think = CK_BODY_NOP;
        return;
    }
    ck_map_set((u16)currActualX, (u16)bridge->tile_y, 0x10E);
    bridge->field_10 += bridge->field_C;
}

void CVort_body_bridge_retract(CkBody *bridge)
{
    s16 currActualX;
    bridge->variant += (s16)ck_sprite_sync;
    if (bridge->variant < 12)
        return;
    bridge->variant -= 12;
    bridge->field_10 -= bridge->field_C;
    currActualX = (s16)bridge->tile_x + bridge->field_10;
    if (map_data_tiles[ck_rowofs[(u16)bridge->tile_y] + (u16)currActualX]
        != 0x10E) {
        bridge->type_ = 0;
        return;
    }
    ck_map_set((u16)currActualX, (u16)bridge->tile_y,
               (u16)bridge->field_E);
}

/* CVort_toggle_switch (src/game/ui.c), including the ep2-only
 * tantalus-ray (0x1DF) and light-switch (0x10F) branches. */
void CVort_toggle_switch(void)
{
    s16 loopVar, var_A, var_6, var_8, bodyIndex;
    u16 sw, plane;
    CVort_engine_setCurSound(0x19);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    /* Looks like a tantalus ray has been activated... oops! The desktop
     * shakes the screen and prints "Oops."; on SNES the flow driver
     * (gameflow.c) presents that, we just end the level. */
    if (map_data_tiles[ck_rowofs[(u16)(keen_tileY + 5)]
                       + (u16)(keen_tileX + 3)] == 0x1DF) {
        g_game.level_finished = CK_LEVEL_END_TANTALUS;
        return;
    }
#endif
    sw = map_data_tiles[ck_rowofs[(u16)keen_tileY] + (u16)keen_tileX];
    switch (sw) {
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
        case 0x10F: /* light switch */
            if (g_game.lights)
                CVort_lights_out();
            else
                CVort_lights_on();
            return;
#endif
        case 0x1E0:
            ck_map_set((u16)keen_tileX, (u16)keen_tileY, 0x1ED);
            break;
        case 0x1ED:
            ck_map_set((u16)keen_tileX, (u16)keen_tileY, 0x1E0);
            break;
        default:
            break;
    }
    /* target bridge tile from the sprite plane (dx in low byte, dy in
     * high byte, both signed) */
    plane = map_data_sprites[ck_rowofs[(u16)keen_tileY] + (u16)keen_tileX];
    var_A = (s16)(plane & 0xFF);
    if (var_A & 0x80)
        var_A -= 0x100;
    var_6 = keen_tileX + var_A;
    var_A = (s16)((plane >> 8) & 0xFF);
    if (var_A & 0x80)
        var_A -= 0x100;
    var_8 = keen_tileY + var_A;
    /* look for an existing bridge body (desktop scan kept verbatim) */
    for (loopVar = 0; loopVar < g_entities.num_bodies; loopVar++) {
        if (!g_entities.bodies[loopVar].type_)
            continue;
        if (g_entities.bodies[loopVar].tile_x != var_6)
            continue;
        if (g_entities.bodies[loopVar].tile_y == var_8)
            break;
    }
    if (loopVar < g_entities.num_bodies) {
        if (g_entities.bodies[loopVar].think == CK_BODY_BRIDGE_RETRACT)
            g_entities.bodies[loopVar].think = CK_BODY_BRIDGE_EXTEND;
        else
            g_entities.bodies[loopVar].think = CK_BODY_BRIDGE_RETRACT;
        return;
    }
    bodyIndex = CVort_add_body();
    g_entities.bodies[bodyIndex].type_ = 2;
    g_entities.bodies[bodyIndex].think = CK_BODY_BRIDGE_EXTEND;
    g_entities.bodies[bodyIndex].tile_x = var_6;
    g_entities.bodies[bodyIndex].tile_y = var_8;
    g_entities.bodies[bodyIndex].variant = 0;
    if (TILEINFO_LEdge[map_data_tiles[ck_rowofs[(u16)var_8]
                                      + (u16)(var_6 + 1)]])
        g_entities.bodies[bodyIndex].field_C = -1;
    else
        g_entities.bodies[bodyIndex].field_C = 1;
    g_entities.bodies[bodyIndex].field_E =
        (s16)map_data_tiles[ck_rowofs[(u16)var_8] + (u16)var_6];
    g_entities.bodies[bodyIndex].field_10 = 0;
}

void CVort_think_dead_sprite(void)
{
    TS.time++;
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort_think_kill_sprite(void)
{
    TS.type_ = CK_OBJ_DEAD;
    TS.time += ck_sprite_sync;
    if (TS.time > 40) {
        TS.time -= 40;
        TS.frame++;
        TS.varB--;
        if (TS.varB == 1)
            TS.think = CK_THINK_DEAD_SPRITE;
    }
    TS.vel_x = 0;
    CVort_do_fall();
    CVort_compute_sprite_delta();
}

void CVort_think_remove_sprite(void)
{
    TS.type_ = 0;
}

void CVort_kill_keen(void)
{
    if (g_game.god_mode || g_game.keen_invincible)
        return;
    g_entities.sprites[0].think = CK_THINK_KEEN_DEATH;
    g_entities.sprites[0].contact = CK_CONTACT_NOP;
    g_entities.sprites[0].pos_y += 0x800;
    g_entities.sprites[0].time = g_entities.sprites[0].vel_x =
        g_entities.sprites[0].vel_y = 0;
    g_entities.sprites[0].frame = 0x16;
    CVort_engine_setCurSound(8);
}

void CVort_kill_keen_temp(void)
{
    TS.think = CK_THINK_KEEN_DEATH;
    TS.contact = CK_CONTACT_NOP;
    TS.pos_y += 0x800;
    TS.time = g_entities.sprites[0].vel_x = g_entities.sprites[0].vel_y =
        TS.vel_x = 0;
    TS.frame = 0x16;
    CVort_engine_setCurSound(8);
}

/* acceleration: l/r speed acceleration */
void CVort_move_left_right(s16 acceleration)
{
    u16 loopVar;
    for (loopVar = 1; loopVar <= ck_sprite_sync; loopVar++) {
        TS.vel_x += acceleration;
        if (TS.vel_x > 0x78)
            TS.vel_x = 0x78;
        else if (TS.vel_x < -0x78)
            TS.vel_x = -0x78;
        if (loopVar != ck_sprite_sync)
            TS.del_x += TS.vel_x;
    }
}

void CVort_pogo_jump(s16 max_height, s16 diff)
{
    u16 loopVar;
    for (loopVar = 1; loopVar <= ck_sprite_sync; loopVar++) {
        TS.vel_y += diff;
        if (TS.vel_y > max_height)
            TS.vel_y = max_height;
        else if (-max_height > TS.vel_y)
            TS.vel_y = -max_height;
        if (loopVar != ck_sprite_sync)
            TS.del_y += TS.vel_y;
    }
}

void CVort_check_ceiling(void)
{
    if (scroll_x_min + 8 > TS.pos_x) {
        TS.vel_x = TS.del_x = 0;
        TS.pos_x = scroll_x_min + 8;
    } else if (TS.pos_x > ceiling_x) {
        TS.vel_x = TS.del_x = 0;
        TS.pos_x = ceiling_x;
    }
    if (TS.pos_y < scroll_y_min) {
        TS.vel_y = TS.del_y = 0;
        TS.pos_y = scroll_y_min;
    } else if (TS.pos_y > ceiling_y) {
        CVort_engine_setCurSound(0x1B);
        CVort_engine_finishCurSound();
        CVort_kill_keen_temp();
    }
}

void CVort_do_fall(void)
{
    u16 loopVar;
    for (loopVar = 1; loopVar <= ck_sprite_sync; loopVar++) {
        TS.vel_y += 3;
        if (TS.vel_y > 200)
            TS.vel_y = 200;
        else if (TS.vel_y < -400)
            TS.vel_y = -400;
        if (loopVar != ck_sprite_sync)
            TS.del_y += TS.vel_y;
    }
}

s16 CVort_compute_sprite_delta(void)
{
    TS.del_x += TS.vel_x * (s16)ck_sprite_sync;
    TS.del_y += TS.vel_y * (s16)ck_sprite_sync;
    return CVort_check_ground();
}

s16 CVort_check_ground(void)
{
    s16 result = 0;
    u16 loopVar, var_4, var_6, var_8, var_A;
    s16 tempNum;
    u16 *row;

    if (TS.del_x > 0xF00)
        TS.del_x = 0xF00;
    else if (TS.del_x < -0xF00)
        TS.del_x = -0xF00;
    if (TS.del_y > 0xF00)
        TS.del_y = 0xF00;
    else if (TS.del_y < -0xF00)
        TS.del_y = -0xF00;

    CK_ADD16(TS.box_y2, TS.del_y);
    CK_ADD16(TS.box_y1, TS.del_y);
    var_4 = CK_TILE(TS.box_x1);
    var_6 = CK_TILE(TS.box_x2);

    if (TS.del_y > 0) {
        if (ck_tile_ofs(&TS.box_y2, (s16)-TS.del_y) != CK_TILE(TS.box_y2)) {
            var_A = CK_TILE(TS.box_y2);
            row = map_data_tiles + ck_rowofs[var_A];
            for (loopVar = var_4; loopVar <= var_6; loopVar++) {
                if (!TILEINFO_UEdge[row[loopVar]])
                    continue; /* no collision */
                TS.vel_y = 0;
                tempNum = (s16)((u16)(((u16 *)&TS.box_y2)[0] + 1) & 0xFFF);
                TS.del_y -= tempNum;
                CK_ADD16(TS.box_y1, (s16)-tempNum);
                CK_ADD16(TS.box_y2, (s16)-tempNum);
                result = 2;
                break;
            }
        }
    } else if (TS.del_y < 0) {
        if (ck_tile_ofs(&TS.box_y1, (s16)-TS.del_y) != CK_TILE(TS.box_y1)) {
            var_8 = CK_TILE(TS.box_y1);
            row = map_data_tiles + ck_rowofs[var_8];
            for (loopVar = var_4; loopVar <= var_6; loopVar++) {
                if (!TILEINFO_DEdge[row[loopVar]])
                    continue;
                TS.vel_y = 0;
                tempNum = (s16)(0x1000 - (((u16 *)&TS.box_y1)[0] & 0xFFF));
                TS.del_y += tempNum;
                CK_ADD16(TS.box_y1, tempNum);
                CK_ADD16(TS.box_y2, tempNum);
                result = 8;
                break;
            }
        }
    }

    CK_ADD16(TS.box_x1, TS.del_x);
    CK_ADD16(TS.box_x2, TS.del_x);
    var_8 = CK_TILE(TS.box_y1);
    var_A = CK_TILE(TS.box_y2);

    if (TS.del_x > 0) {
        if (ck_tile_ofs(&TS.box_x2, (s16)-TS.del_x) != CK_TILE(TS.box_x2)) {
            var_6 = CK_TILE(TS.box_x2);
            for (loopVar = var_8; loopVar <= var_A; loopVar++) {
                if (!TILEINFO_LEdge[map_data_tiles[ck_rowofs[loopVar] + var_6]])
                    continue;
                TS.vel_x = 0;
                tempNum = (s16)((u16)(((u16 *)&TS.box_x2)[0] + 1) & 0xFFF);
                TS.del_x -= tempNum;
                CK_ADD16(TS.box_x1, (s16)-tempNum);
                CK_ADD16(TS.box_x2, (s16)-tempNum);
                result |= 4;
                break;
            }
        }
    } else if (TS.del_x < 0) {
        if (ck_tile_ofs(&TS.box_x1, (s16)-TS.del_x) != CK_TILE(TS.box_x1)) {
            var_4 = CK_TILE(TS.box_x1);
            for (loopVar = var_8; loopVar <= var_A; loopVar++) {
                if (!TILEINFO_REdge[map_data_tiles[ck_rowofs[loopVar] + var_4]])
                    continue;
                TS.vel_x = 0;
                tempNum = (s16)(0x1000 - (((u16 *)&TS.box_x1)[0] & 0xFFF));
                TS.del_x += tempNum;
                CK_ADD16(TS.box_x1, tempNum);
                CK_ADD16(TS.box_x2, tempNum);
                result |= 1;
                break;
            }
        }
    }

    return result;
}

/* Latch the engine camera (pixels) from the scroll position (world
 * units). The physics keeps the original DOS scroll bounds; the render
 * layer's own max keeps the 256x224 window inside the map. */
static void ck_sync_camera(void)
{
    s16 px = (s16)(scroll_x >> 8);
    s16 py = (s16)(scroll_y >> 8);
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if ((u16)px > ck_cam_px_max) px = (s16)ck_cam_px_max;
    if ((u16)py > ck_cam_py_max) py = (s16)ck_cam_py_max;
    ck_cam_px = (u16)px;
    ck_cam_py = (u16)py;
}

void CVort_do_scrolling(void)
{
    s16 sprDelX, sprDelY;
    s32 tempNum;

    if ((g_entities.sprites[0].think == CK_THINK_KEEN_EXIT) ||
        (g_entities.sprites[0].think == CK_THINK_KEEN_DEATH))
        return;
    sprDelX = g_entities.sprites[0].del_x;
    sprDelY = g_entities.sprites[0].del_y;

    if (sprDelX > 0) { /* XScrollHi */
        tempNum = g_entities.sprites[0].pos_x - scroll_x;
        if (tempNum > 0xB000L) {
            scroll_x += sprDelX;
            if (scroll_x > scroll_x_max)
                scroll_x = scroll_x_max;
        }
    } else if (sprDelX < 0) { /* XScrollLo */
        tempNum = g_entities.sprites[0].pos_x - scroll_x;
        if (tempNum < 0x9000L) {
            scroll_x += sprDelX;
            if (scroll_x < scroll_x_min)
                scroll_x = scroll_x_min;
        }
    }

    if (sprDelY > 0) { /* YScrollHi */
        tempNum = g_entities.sprites[0].pos_y - scroll_y;
        if (tempNum > 0x7000L) {
            scroll_y += sprDelY;
            if (scroll_y > scroll_y_max)
                scroll_y = scroll_y_max;
        }
    } else if (sprDelY < 0) { /* YScrollLo */
        tempNum = g_entities.sprites[0].pos_y - scroll_y;
        if (tempNum < 0x3000L) {
            scroll_y += sprDelY;
            if (scroll_y < scroll_y_min)
                scroll_y = scroll_y_min;
        }
    }

    ck_sync_camera();
}

/* Returns 0 if sprite is to be updated, 1 if not */
s16 CVort_sprite_active_screen(void)
{
    s16 scaledX = CK_W2T(TS.pos_x), scaledY = CK_W2T(TS.pos_y);
    if (TS.pos_y < 0)
        TS.pos_y = 0;
    if ((TS.pos_x > map_width) || (TS.pos_x < 0) || (TS.pos_y > map_height)) {
        TS.type_ = 0;
        return 1;
    }
    if ((scroll_x_tile - 8 <= scaledX) && (scroll_y_tile - 8 <= scaledY) &&
        (scroll_x_tile + 28 >= scaledX) && (scroll_y_tile + 18 >= scaledY))
        return 0;
    if (TS.type_ >= CK_OBJ_ONEBEFORESHOT) {
        TS.type_ = 0;
        return 1;
    }
    TS.active = 0;
    return 1;
}

/* Returns 1 if there is a collision, and 0 otherwise */
s16 CVort_detect_sprite_col(CkSprite *spr_0, CkSprite *spr_1)
{
    /* separate ifs: 816-tcc miscompiles "!a || !b"; u16-half compares
     * via ck_lt32 (see the split-u16 helper block above) */
    if (((u16 *)&spr_0->box_x1)[0] == 0)
        if (((u16 *)&spr_0->box_x1)[1] == 0)
            return 0;
    if (((u16 *)&spr_1->box_x1)[0] == 0)
        if (((u16 *)&spr_1->box_x1)[1] == 0)
            return 0;
    if (ck_lt32(&spr_0->box_x2, &spr_1->box_x1))
        return 0;
    if (ck_lt32(&spr_0->box_y2, &spr_1->box_y1))
        return 0;
    if (ck_lt32(&spr_1->box_x2, &spr_0->box_x1))   /* a.x1 > b.x2 */
        return 0;
    if (ck_lt32(&spr_1->box_y2, &spr_0->box_y1))   /* a.y1 > b.y2 */
        return 0;
    return 1;
}

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE != 1
/* word-copy between a live sprite slot and temp_sprite (no memcpy) */
static void ck_phys_sprite_copy(CkSprite *dst, const CkSprite *src)
{
    u8 i;
    u16 *d = (u16 *)dst;
    const u16 *s = (const u16 *)src;
    for (i = 0; i < sizeof(CkSprite) / 2; i++)
        d[i] = s[i];
}

/* CVort_carry_keen (src/game/physics.c:343): Keen rides / is pushed by a
 * moving platform or Scrub. Operates on temp_sprite like the desktop. */
void CVort_carry_keen(CkSprite *keen, CkSprite *carrier)
{
    s16 del_x_dif, del_y_dif, boxX_dif, boxY_dif;

    ck_phys_sprite_copy(&TS, keen);

    if (carrier->pos_x < TS.pos_x) {
        del_x_dif = (s16)(carrier->del_x - TS.del_x + 2);
        boxX_dif = (s16)((u16)carrier->box_x2 - (u16)TS.box_x1 + 1);
    } else {
        del_x_dif = (s16)(TS.del_x - carrier->del_x + 2);
        boxX_dif = (s16)((u16)TS.box_x2 - (u16)carrier->box_x1 + 1);
    }
    (void)del_x_dif; /* computed but unused by the original too */

    if (carrier->pos_y < TS.pos_y) {
        del_y_dif = (s16)(carrier->del_y - TS.del_y + 2);
        boxY_dif = (s16)((u16)carrier->box_y2 - (u16)TS.box_y1 + 1);
    } else {
        del_y_dif = (s16)(TS.del_y - carrier->del_y + 2);
        boxY_dif = (s16)((u16)TS.box_y2 - (u16)carrier->box_y1 + 1);
    }

    TS.del_y = TS.del_x = 0;

    if (del_y_dif < boxY_dif) {
        if (carrier->pos_x > TS.pos_x) {
            TS.del_x = -boxX_dif;
            if (carrier->vel_x < TS.vel_x)
                TS.vel_x = carrier->vel_x;
        } else {
            TS.del_x = boxX_dif;
            if (carrier->vel_x > TS.vel_x)
                TS.vel_x = carrier->vel_x;
        }
    } else {
        if (carrier->pos_y > TS.pos_y) {
            /* Keen stands on top of the carrier */
            TS.del_x = carrier->del_x;
            TS.del_y = (s16)(-boxY_dif - 0x80);
            if (TS.think == CK_THINK_KEEN_JUMP_AIR) {
                TS.think = CK_THINK_KEEN_GROUND;
            } else if (TS.think == CK_THINK_KEEN_POGO_AIR) {
                TS.think = CK_THINK_KEEN_POGO;
                TS.time = 0;
                TS.varB = TS.vel_x;
                TS.vel_x = 0;
            }
            TS.varD = 1;
            TS.vel_y = carrier->vel_y;
            if (TS.vel_y < 0)
                TS.vel_y /= 2;
        } else {
            TS.del_y = boxY_dif;
            /* don't get "lifted" off a quickly falling platform */
            if (carrier->vel_y > TS.vel_y)
                TS.vel_y = carrier->vel_y;
        }
    }

    CVort_check_ground();
    TS.pos_x += TS.del_x;
    TS.pos_y += TS.del_y;
    CVort_update_sprite_hitbox();
    ck_phys_sprite_copy(keen, &TS);
    CVort_do_scrolling();
}

/* CVort_push_keen (src/game/physics.c:438; ep3 Meep/Vortininja). */
void CVort_push_keen(CkSprite *keen, CkSprite *pusher)
{
    ck_phys_sprite_copy(&TS, keen);

    /* box coords are non-negative in-level: >>1 == /2 */
    if (((TS.box_x2 + TS.box_x1) >> 1) < ((pusher->box_x2 + pusher->box_x1) >> 1)) {
        TS.del_x = (s16)(-(s16)(TS.box_x2 - pusher->box_x1 + 1));
        if (TS.del_x > 120)      /* vanilla quirk kept: sets del_y */
            TS.del_y = 120;
    } else {
        TS.del_x = (s16)(pusher->box_x2 - TS.box_x1 + 1);
        if (TS.del_x < -120)
            TS.del_y = -120;
    }
    CVort_check_ground();
    TS.pos_x += TS.del_x;
    TS.pos_y += TS.del_y;
    CVort_update_sprite_hitbox();
    ck_phys_sprite_copy(keen, &TS);
    CVort_do_scrolling();
}
#endif /* episode 2/3 */

/* Handles Keen's collision with various kinds of tiles.
 *
 * SNES M3 notes: door bodies (slide animation) are deferred to M4 - a
 * carried key still opens the door, but the two door tiles are cleared
 * at once. Message/switch tiles are stubs. Mutated tiles only reach
 * VRAM when the camera re-streams them (renderer dirty-tile API is an
 * M4 item).
 */
void CVort_keen_bgtile_col(void)
{
    s16 tileleft, tileright, tiletop, tilebottom;
    s16 currX, currY;
    u16 currTilePos;
    s16 currTileType;
    /* offset crosses a HiROM bank; (u32)ptr casts drop the bank byte,
     * so go through ck_far_ofs (levelload.h) */
    const s16 *points_tbl = (const s16 *)ck_far_ofs(ck_exe_image,
                                                    CK_POINTS_TBL_OFFSET);

    if (g_entities.sprites[0].think == CK_THINK_KEEN_DEATH)
        return;
    g_game.keen_switch = 0;
    tileleft   = (s16)CK_TILE(g_entities.sprites[0].box_x1);
    tileright  = (s16)CK_TILE(g_entities.sprites[0].box_x2);
    tiletop    = (s16)CK_TILE(g_entities.sprites[0].box_y1);
    tilebottom = (s16)CK_TILE(g_entities.sprites[0].box_y2);

    for (currX = tileleft; currX <= tileright; currX++)
        for (currY = tiletop; currY <= tilebottom; currY++) {
            currTilePos = ck_rowofs[(u16)currY] + (u16)currX;
            currTileType = TILEINFO_Type[map_data_tiles[currTilePos]];
            if (!currTileType)
                continue;
            switch (currTileType) {
                case 1: /* hazardous tile: kills Keen immediately */
                    CVort_kill_keen();
                    break;
                case 2: /* a door */
                case 3:
                case 4:
                case 5:
                    if (keen_gp.stuff[3 + currTileType]) {
                        CVort_open_door(currX, currY);
                    } else if (g_entities.sprites[0].del_x > 0) {
                        g_entities.sprites[0].pos_x &= 0xFFFFF000L;
                    } else {
                        g_entities.sprites[0].pos_x =
                            (g_entities.sprites[0].pos_x + 0x1000) & 0xFFFFF000L;
                    }
                    break;
                case 6: /* items that add points */
                case 7:
                case 8:
                case 9:
                case 10:
                    CVort_add_score(points_tbl[currTileType - 6]);
                    CVort_engine_setCurSound(9);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
                    ck_map_set((u16)currX, (u16)currY,
                        (u16)((map_data_tiles[currTilePos] / 0xD) * 0xD));
#else
                    if (map_data_tiles[currTilePos] < 0x131)
                        ck_map_set((u16)currX, (u16)currY, 0x8F);
                    else
                        ck_map_set((u16)currX, (u16)currY, 0x114);
#endif
                    break;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
                case 11: /* one of the four BwB parts */
                case 12:
                case 13:
                case 14:
                    if (currTileType == 11)
                        keen_gp.stuff[0] = 1;
                    if (currTileType == 12)
                        keen_gp.stuff[4] = 1;
                    if (currTileType == 13)
                        keen_gp.stuff[1] = 1;
                    if (currTileType == 14)
                        keen_gp.stuff[2] = 1;
                    CVort_add_score(10000);
                    CVort_engine_setCurSound(0xB);
                    ck_map_set((u16)currX, (u16)currY, 0x8F);
                    break;
#endif
                case 15: /* ammo or... */
                case 16: /* ...pogo */
                    if (currTileType == 15)
                        keen_gp.ammo += 5;
                    if (currTileType == 16)
                        keen_gp.stuff[3] = 1;
                    CVort_engine_setCurSound(0xA);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
                    ck_map_set((u16)currX, (u16)currY, 0x8F);
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
                    if (map_data_tiles[currTilePos] < 0x131)
                        ck_map_set((u16)currX, (u16)currY, 0x8F);
                    else
                        ck_map_set((u16)currX, (u16)currY, 0x114);
#else
                    ck_map_set((u16)currX, (u16)currY,
                        (u16)((map_data_tiles[currTilePos] / 0xD) * 0xD));
#endif
                    break;
                case 17: /* exit door */
                    if (g_entities.sprites[0].think != CK_THINK_KEEN_GROUND)
                        break;
                    CVort_engine_setCurSound(0xF);
                    g_entities.sprites[0].think = CK_THINK_KEEN_EXIT;
                    g_entities.sprites[0].contact = CK_CONTACT_NOP;
                    g_entities.sprites[0].time = currX + 2;
                    g_entities.sprites[0].varB = currY;
                    break;
                case 18: /* a key for some door */
                case 19:
                case 20:
                case 21:
                    keen_gp.stuff[currTileType - 13] = 1;
                    CVort_engine_setCurSound(0x20);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
                    ck_map_set((u16)currX, (u16)currY,
                        (u16)((map_data_tiles[currTilePos] / 0xD) * 0xD));
#else
                    ck_map_set((u16)currX, (u16)currY, 0x8F);
#endif
                    break;
                case 22: /* message statue - text UI is a later milestone */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
                    if (g_game.current_level == 0xB)
                        ck_map_set((u16)currX, (u16)currY, 0x1B2);
                    else
                        ck_map_set((u16)currX, (u16)currY, 0x13B);
#else
                    ck_map_set((u16)currX, (u16)currY, 0x8F);
#endif
                    break;
                case 23: /* a switch (CVort_toggle_switch fires on pogo key) */
                case 25:
                case 26:
                    g_game.keen_switch = 1;
                    keen_tileX = currX;
                    keen_tileY = currY;
                    break;
                case 24: /* secret level teleporter */
                    g_game.level_finished = CK_LEVEL_END_SECRET;
                    break;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
                case 27: /* ankh (temporary invincibility) */
                    g_game.keen_invincible += 1400;
                    CVort_engine_setCurSound(0x2A); /* snd_ankh */
                    ck_map_set((u16)currX, (u16)currY,
                        (u16)((map_data_tiles[currTilePos] / 0xD) * 0xD));
                    break;
                case 28: /* single ammo pickup */
                    keen_gp.ammo++;
                    ck_map_set((u16)currX, (u16)currY,
                        (u16)((map_data_tiles[currTilePos] / 0xD) * 0xD));
                    CVort_engine_setCurSound(0xA);
                    break;
#endif
                default:
                    break;
            }
        }
}
