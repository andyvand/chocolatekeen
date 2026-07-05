/* worldmap.c - SNES transcription of the world map session from
 * src/game/worldmap.c (CVort_draw_worldmap's map loop and its helpers)
 * plus the ep1 map-sprite handler (ship dialog stub + teleporters +
 * secret city) from src/episodes/episode1.c.
 *
 * 816-tcc rules observed: no negated short-circuit chains, all runtime
 * state initialized in functions, positions s32, map indexing through
 * ck_rowofs, multi-bank EXE reads through ck_far_ofs.
 *
 * SNES adaptations (noted inline):
 *  - map movement is scaled by ck_sprite_sync (0x100/tick ~= the DOS
 *    0x400/frame at ~4 ticks/frame) instead of a fixed per-frame step;
 *  - the teleporter tile flash animation is skipped (its tiles are not
 *    in the level-80 VRAM tile set); the final tile mutations and the
 *    teleport itself are kept. A teleport re-fills the BG window under
 *    forced blank because the camera jumps arbitrarily far.
 */
#include "game/game_state.h"
#include "game/sprites.h"
#include "game/physics.h"
#include "game/gameplay.h"
#include "game/worldmap.h"
#include "game/ui.h"
#include "engine/levelload.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/input.h"
#include "engine/timer.h"
#include "data_format.h"
#include "snes_data_gen.h"

s32 keen_wmap_x_pos, keen_wmap_y_pos;

static s32 wmap_scroll_x, wmap_scroll_y;

typedef struct CkMaplevel_T {
    u16 tx, ty;                 /* level marker position (tiles) */
    s16 type_;                  /* 0 = 1x1 entry, 1 = 2x2 entry  */
} CkMaplevel;
static CkMaplevel wmaplevels[16];

typedef struct CkTeleporter_T {
    s32 destX, destY;           /* ep1: world units; ep3: tile coords */
    s16 isOnSnow;               /* ep1: snow flag; ep3: dest tele idx */
} CkTeleporter;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
#define CK_TELE_COUNT 16
#else
#define CK_TELE_COUNT 3
#endif
static CkTeleporter teleporters[CK_TELE_COUNT];

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
/* Messie state (desktop globals: messie_* in src/core/globals.h). All
 * runtime-initialized (816-tcc landmine #2). */
static s32 messie_xpos, messie_ypos;
static s16 messie_del_x, messie_del_y;
static u16 messie_mounted, messie_frame, messie_time_to_climb;
static u16 messie_move_tics;
static u16 messie_x_T, messie_y_T;
static u8  s_messie_placed;     /* Messie found + initialized this game */
#endif

static u8 s_placed;             /* Keen has a map position           */
static u8 s_secret_pending;     /* arrive at teleporters[2] on entry */
static u8 s_wait_release;       /* swallow held jump/pogo            */
static u16 s_sprite_x, s_sprite_y; /* tile of the triggered map sprite */

/* world map tiles: teleporter resting art (src/core/constants.h) */
#define CK_TILES_TELEDIRT 0x145
#define CK_TILES_TELESNOW 0x63
#define CK_TELEPORTSND    0x12

/* ---- boot binding ------------------------------------------------------ */

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
/* episode1_engine.c: teleporters[0..2] destX/destY (dwords, world
 * units) + isOnSnow (word), 10 bytes per entry at EXE offset 0x158DE. */
#define CK_EP1_TELE_OFFSET 0x158DEUL

static u32 rd_u32(const u8 *p)
{
    /* assemble in two u16 halves (816-tcc emulates 32-bit ops) */
    u16 lo = (u16)((u16)p[0] | ((u16)p[1] << 8));
    u16 hi = (u16)((u16)p[2] | ((u16)p[3] << 8));
    return (u32)lo | ((u32)hi << 16);
}
#endif

void ck_worldmap_init(void)
{
    u8 i;
    for (i = 0; i < 16; i++) {
        wmaplevels[i].tx = 0;
        wmaplevels[i].ty = 0;
        wmaplevels[i].type_ = 0;
    }
    for (i = 0; i < CK_TELE_COUNT; i++) {
        teleporters[i].destX = 0;
        teleporters[i].destY = 0;
        teleporters[i].isOnSnow = 0;
    }
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    messie_xpos = messie_ypos = 0;
    messie_del_x = messie_del_y = 0;
    messie_mounted = 0;
    messie_frame = 0x82;        /* spr_messield1 */
    messie_time_to_climb = 0;
    messie_move_tics = 8;
    messie_x_T = messie_y_T = 0;
    s_messie_placed = 0;
#endif
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    {
        const u8 *p = ck_far_ofs(ck_exe_image, CK_EP1_TELE_OFFSET);
        for (i = 0; i < 3; i++) {
            teleporters[i].destX = (s32)rd_u32(p);
            teleporters[i].destY = (s32)rd_u32(p + 4);
            teleporters[i].isOnSnow =
                (s16)((u16)p[8] | ((u16)p[9] << 8));
            p += 10;
        }
    }
#endif
    keen_wmap_x_pos = keen_wmap_y_pos = 0;
    wmap_scroll_x = wmap_scroll_y = 0;
    s_placed = 0;
    s_secret_pending = 0;
    s_wait_release = 0;
    s_sprite_x = s_sprite_y = 0;
}

void ck_worldmap_new_game(void)
{
    s_placed = 0;
    s_secret_pending = 0;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    s_messie_placed = 0;
#endif
}

void ck_worldmap_secret_city(void)
{
    s_secret_pending = 1;
}

/* ---- CVort_place_keen_on_worldmap ------------------------------------- */
static void CVort_place_keen_on_worldmap(void)
{
    u16 currX, currY;
    const u16 *sp = map_data_sprites;
    for (currY = 0; currY < map_height_tile; currY++)
        for (currX = 0; currX < map_width_tile; currX++) {
            if (*sp++ != 0xFF)
                continue;
            keen_wmap_x_pos = CK_T2W(currX);
            keen_wmap_y_pos = CK_T2W(currY);
            return;
        }
}

/* ---- CVort_mark_cities_done (GAMEVER_KEEN1 paths) ---------------------- */
static void CVort_mark_cities_done(void)
{
    u16 pos_x, pos_y, mapEntry;
    u8 i;
    const u16 *row;
    for (i = 0; i < 16; i++) {
        wmaplevels[i].tx = 0;
        wmaplevels[i].ty = 0;
        wmaplevels[i].type_ = 0;
    }
    for (pos_y = 0; pos_y < map_height_tile; pos_y++) {
        row = map_data_sprites + ck_rowofs[pos_y];
        for (pos_x = 0; pos_x < map_width_tile; pos_x++) {
            mapEntry = (u16)(row[pos_x] & 0x7FFF);
            if (mapEntry == 0)
                continue;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
            /* ep3 teleporters live in the sprite plane: 0xFst with
             * s = this pad's index, t = destination pad's index. */
            if ((mapEntry & 0xF00) == 0xF00) {
                u16 teleIndex = (mapEntry >> 4) & 0xF;
                teleporters[teleIndex].isOnSnow = (s16)(mapEntry & 0xF);
                teleporters[teleIndex].destX = pos_x;
                teleporters[teleIndex].destY = pos_y;
            }
#endif
            if (mapEntry > 0x10)
                continue;
            if (wmaplevels[mapEntry - 1].tx)
                continue;
            wmaplevels[mapEntry - 1].tx = pos_x;
            wmaplevels[mapEntry - 1].ty = pos_y;
            wmaplevels[mapEntry - 1].type_ = 0;
            if (mapEntry != (u16)(row[pos_x + 1] & 0x7FFF))
                continue;
            pos_x++;
            wmaplevels[mapEntry - 1].type_ = 1;
        }
    }
}

/* CVort_draw_worldmap's completed-level pass: clear the sprite cells and
 * stamp the "done" tiles (KEEN1 group 0x4D / 0x4E..0x51). Runs before
 * ck_render_level_init, so plain map writes suffice. */
static void wm_stamp_done_levels(void)
{
    u8 i;
    u16 x, y, ro0, ro1;
    for (i = 0; i < 16; i++) {
        if (!keen_gp.levels[i])
            continue;
        x = wmaplevels[i].tx;
        y = wmaplevels[i].ty;
        if (!x)
            continue;           /* level not on this map */
        ro0 = ck_rowofs[y];
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
        if (wmaplevels[i].type_ == 0) {
            map_data_sprites[ro0 + x] = 0;
            map_data_tiles[ro0 + x] = 0x38;
        } else {
            ro1 = ck_rowofs[(u16)(y + 1)];
            map_data_sprites[ro0 + x] = 0;
            map_data_sprites[ro0 + x + 1] = 0;
            map_data_sprites[ro1 + x] = 0;
            map_data_sprites[ro1 + x + 1] = 0;
            map_data_tiles[ro0 + x] = 0x34;
            map_data_tiles[ro0 + x + 1] = 0x35;
            map_data_tiles[ro1 + x] = 0x36;
            map_data_tiles[ro1 + x + 1] = 0x37;
        }
#else
        if (wmaplevels[i].type_ == 0) {
            map_data_sprites[ro0 + x] = 0;
            map_data_tiles[ro0 + x] = 0x4D;
        } else {
            ro1 = ck_rowofs[(u16)(y + 1)];
            map_data_sprites[ro0 + x] = 0;
            map_data_sprites[ro0 + x + 1] = 0;
            map_data_sprites[ro1 + x] = 0;
            map_data_sprites[ro1 + x + 1] = 0;
            map_data_tiles[ro0 + x] = 0x4E;
            map_data_tiles[ro0 + x + 1] = 0x4F;
            map_data_tiles[ro1 + x] = 0x50;
            map_data_tiles[ro1 + x + 1] = 0x51;
        }
#endif
    }
}

/* ---- camera ------------------------------------------------------------ */
static void wm_clamp_scroll(void)
{
    if (scroll_x < scroll_x_min)
        scroll_x = scroll_x_min;
    if (scroll_y < scroll_y_min)
        scroll_y = scroll_y_min;
    if (scroll_x > scroll_x_max)
        scroll_x = scroll_x_max;
    if (scroll_y > scroll_y_max)
        scroll_y = scroll_y_max;
}

static void wm_sync_camera(void)
{
    s16 px, py;
    u16 maxx, maxy, wpx, hpx;
    scroll_x_tile = CK_W2T(scroll_x);
    scroll_y_tile = CK_W2T(scroll_y);
    px = (s16)(scroll_x >> 8);
    py = (s16)(scroll_y >> 8);
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    /* Clamp against the CURRENT map's pixel bounds, computed from the
     * loaded dimensions. ck_cam_px_max/py_max belong to the renderer
     * and are only refreshed inside ck_render_level_init - which runs
     * AFTER ck_worldmap_enter, so at map-entry time they still hold the
     * PREVIOUS session's bounds (a level's, or boot garbage). Clamping
     * against them squashed the camera, so the initial fill painted the
     * wrong window: the "Keen walking in space" world-map corruption,
     * identical in all three episodes. */
    wpx = (u16)(map_width_tile << 4);
    hpx = (u16)(map_height_tile << 4);
    maxx = (wpx > 256) ? (u16)(wpx - 256) : 0;
    maxy = (hpx > 224) ? (u16)(hpx - 224) : 0;
    if ((u16)px > maxx) px = (s16)maxx;
    if ((u16)py > maxy) py = (s16)maxy;
    ck_cam_px = (u16)px;
    ck_cam_py = (u16)py;
}

/* ---- session entry ------------------------------------------------------ */
void ck_worldmap_enter(void)
{
    CkSprite *k = &g_entities.sprites[0];
    u8 i;
    u8 *kb = (u8 *)k;

    ck_level_setup_bounds();
    CVort_mark_cities_done();
    wm_stamp_done_levels();

    for (i = 0; i < sizeof(CkSprite); i++)
        kb[i] = 0;
    k->type_ = CK_OBJ_KEEN;
    k->think = CK_THINK_NOP;    /* map think runs here, not via dispatch */
    k->contact = CK_CONTACT_NOP;
    k->frame = CK_SPR_MAPKEEN_D; /* spr_mapkeend1 */
    k->active = 1;
    g_entities.num_sprites = 1;
    g_entities.num_bodies = 0;

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    ck_lights_reset();          /* recover bright palettes after a dark death */
#endif
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    /* Messie spawn scan (desktop CVort_draw_worldmap KEEN3 head): the
     * first 0x2000 sprite value down the middle column. Runs once per
     * game; her position persists across level entries. */
    if (!s_messie_placed) {
        u16 var4, mid = (u16)(map_width_tile >> 1);
        for (var4 = 0; var4 < map_height_tile; var4++) {
            if (map_data_sprites[ck_rowofs[var4] + mid] == 0x2000) {
                messie_mounted = 0;
                messie_time_to_climb = 0;
                messie_x_T = mid;
                messie_y_T = var4;
                messie_del_x = -0x200;
                messie_del_y = 0;
                messie_xpos = CK_T2W(mid);
                messie_ypos = CK_T2W(var4);
                messie_frame = 0x82; /* spr_messield1 */
                messie_move_tics = 8;
                break;
            }
        }
        s_messie_placed = 1;
    }
#endif

    if (!s_placed) {
        CVort_place_keen_on_worldmap();
        /* screenX/Y = mapX - 0x9000, mapY - 0x3000 (0xFFFF7000 adds) */
        wmap_scroll_x = keen_wmap_x_pos - 0x9000L;
        wmap_scroll_y = keen_wmap_y_pos - 0x3000L;
        s_placed = 1;
    }
    if (s_secret_pending) {
        /* CVort1_handle_secret_city: arrive at the hidden-city
         * teleporter (flash animation skipped, see file header). */
        u16 tx, ty, tile;
        s_secret_pending = 0;
        CVort_engine_setCurSound(CK_TELEPORTSND);
        keen_wmap_x_pos = teleporters[2].destX;
        keen_wmap_y_pos = teleporters[2].destY;
        wmap_scroll_x = keen_wmap_x_pos - 0x9000L;
        wmap_scroll_y = keen_wmap_y_pos - 0x3000L;
        tx = (u16)CK_W2T(keen_wmap_x_pos);
        ty = (u16)CK_W2T(keen_wmap_y_pos);
        tile = CK_TILES_TELEDIRT;
        if (teleporters[2].isOnSnow)
            tile = CK_TILES_TELESNOW;
        map_data_tiles[ck_rowofs[ty] + tx] = tile;
    }

    k->pos_x = keen_wmap_x_pos;
    k->pos_y = keen_wmap_y_pos;
    scroll_x = wmap_scroll_x;
    scroll_y = wmap_scroll_y;
    wm_clamp_scroll();
    wm_sync_camera();

    g_game.current_level = 80;
    g_game.level_finished = 0;
    g_game.on_world_map = 1;
    g_game.wmap_sprite_on = 0;
    g_game.wmap_col = 0x8000;
    g_game.god_mode = 0;
    s_wait_release = 1;         /* swallow the button that exited a level */

    ck_input_old.direction = 8;
    ck_input_old.but1jump = ck_input_old.but2pogo = 0;
}

/* ---- CVort_check_world_map_col ------------------------------------------ */
static CkSprite s_keenMap;      /* desktop keen_map[0] scratch copy */

static s16 CVort_check_world_map_col(CkSprite *sprite)
{
    s16 blocking = 0;
    s16 x1_tile, x2_tile, y1_tile, y2_tile;
    s16 tempNum, x, y;
    u8 hit;
    u16 idx;

    if (g_game.god_mode)
        return 0;
    sprite->del_x += sprite->vel_x * (s16)ck_sprite_sync;
    sprite->del_y += sprite->vel_y * (s16)ck_sprite_sync;
    {
        u16 i;
        u16 *d = (u16 *)&s_keenMap;
        const u16 *s = (const u16 *)sprite;
        for (i = 0; i < sizeof(CkSprite) / 2; i++)
            d[i] = s[i];
    }

    /* vertical move first */
    s_keenMap.box_y2 += s_keenMap.del_y;
    s_keenMap.box_y1 += s_keenMap.del_y;
    x1_tile = CK_W2T(s_keenMap.box_x1);
    x2_tile = CK_W2T(s_keenMap.box_x2);

    if (s_keenMap.del_y > 0) {  /* down */
        if (CK_W2T(s_keenMap.box_y2) !=
            CK_W2T(s_keenMap.box_y2 - s_keenMap.del_y)) {
            y2_tile = CK_W2T(s_keenMap.box_y2);
            for (x = x1_tile; x <= x2_tile; x++) {
                idx = (u16)(ck_rowofs[(u16)y2_tile] + (u16)x);
                hit = 0;        /* blocked by tile edge OR sprite flag */
                if (TILEINFO_UEdge[map_data_tiles[idx]])
                    hit = 1;
                else if (map_data_sprites[idx] & g_game.wmap_col)
                    hit = 1;
                if (!hit)
                    continue;
                sprite->vel_y = 0;
                tempNum = (s16)((u16)(s_keenMap.box_y2 + 1) & 0xFFF);
                sprite->del_y -= tempNum;
                s_keenMap.box_y1 -= tempNum;
                s_keenMap.box_y2 -= tempNum;
                blocking = 1;
                break;
            }
        }
    } else if (s_keenMap.del_y < 0) { /* up */
        if (CK_W2T(s_keenMap.box_y1) !=
            CK_W2T(s_keenMap.box_y1 - s_keenMap.del_y)) {
            y1_tile = CK_W2T(s_keenMap.box_y1);
            for (x = x1_tile; x <= x2_tile; x++) {
                idx = (u16)(ck_rowofs[(u16)y1_tile] + (u16)x);
                hit = 0;
                if (TILEINFO_DEdge[map_data_tiles[idx]])
                    hit = 1;
                else if (map_data_sprites[idx] & g_game.wmap_col)
                    hit = 1;
                if (!hit)
                    continue;
                sprite->vel_y = 0;
                tempNum = (s16)(0x1000 - ((u16)s_keenMap.box_y1 & 0xFFF));
                sprite->del_y += tempNum;
                s_keenMap.box_y1 += tempNum;
                s_keenMap.box_y2 += tempNum;
                blocking = 1;
                break;
            }
        }
    }

    /* now horizontal */
    s_keenMap.box_x1 += s_keenMap.del_x;
    s_keenMap.box_x2 += s_keenMap.del_x;
    y1_tile = CK_W2T(s_keenMap.box_y1);
    y2_tile = CK_W2T(s_keenMap.box_y2);

    if (s_keenMap.del_x > 0) {  /* right */
        if (CK_W2T(s_keenMap.box_x2) !=
            CK_W2T(s_keenMap.box_x2 - s_keenMap.del_x)) {
            x2_tile = CK_W2T(s_keenMap.box_x2);
            for (y = y1_tile; y <= y2_tile; y++) {
                idx = (u16)(ck_rowofs[(u16)y] + (u16)x2_tile);
                hit = 0;
                if (TILEINFO_LEdge[map_data_tiles[idx]])
                    hit = 1;
                else if (map_data_sprites[idx] & g_game.wmap_col)
                    hit = 1;
                if (!hit)
                    continue;
                sprite->vel_x = 0;
                tempNum = (s16)((u16)(s_keenMap.box_x2 + 1) & 0xFFF);
                sprite->del_x -= tempNum;
                s_keenMap.box_x1 -= tempNum;
                s_keenMap.box_x2 -= tempNum;
                blocking = 1;
                break;
            }
        }
    } else if (s_keenMap.del_x < 0) { /* left */
        if (CK_W2T(s_keenMap.box_x1) !=
            CK_W2T(s_keenMap.box_x1 - s_keenMap.del_x)) {
            x1_tile = CK_W2T(s_keenMap.box_x1);
            for (y = y1_tile; y <= y2_tile; y++) {
                idx = (u16)(ck_rowofs[(u16)y] + (u16)x1_tile);
                hit = 0;
                if (TILEINFO_REdge[map_data_tiles[idx]])
                    hit = 1;
                else if (map_data_sprites[idx] & g_game.wmap_col)
                    hit = 1;
                if (!hit)
                    continue;
                sprite->vel_x = 0;
                tempNum = (s16)(0x1000 - ((u16)s_keenMap.box_x1 & 0xFFF));
                sprite->del_x += tempNum;
                s_keenMap.box_x1 += tempNum;
                s_keenMap.box_x2 += tempNum;
                blocking |= 1;
                break;
            }
        }
    }

    return blocking;
}

/* ---- CVort_update_sprite_hitbox_wmap ------------------------------------ */
static void CVort_update_sprite_hitbox_wmap(CkSprite *s)
{
    u16 idx = (u16)((s->frame << 4) + ((((u16)s->pos_x >> 9) & 3) << 2));
    const s16 *h = ck_sprite_hitboxes + idx;
    s->box_x1 = s->pos_x + h[0];
    s->box_x2 = s->pos_x + h[2];
    s->box_y1 = s->pos_y + h[1];
    s->box_y2 = s->pos_y + h[3];
}

/* ---- ep1 map sprites: ship dialog + teleporters -------------------------- */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
/* Returns 1 when the map sprite was handled here (stay on the map). */
static u8 CVort1_worldmap_sprites(u16 map_sprite_standing, CkSprite *k)
{
    s16 dstTeleportId, srcTeleportId;
    u16 tx, ty, tile;

    if (map_sprite_standing == 0x14) {
        /* BWB ship: the "missing parts" dialog is M6 UI; the ep1 win
         * itself triggers on collecting all four parts (gameflow.c). */
        return 1;
    }
    if ((map_sprite_standing & 0x20) != 0x20)
        return 0;

    /* teleporter */
    dstTeleportId = (s16)(map_sprite_standing & 3) - 1;
    srcTeleportId = (s16)((map_sprite_standing >> 2) & 3) - 1;
    if (dstTeleportId < 0)
        return 1;
    if (dstTeleportId > 2)
        return 1;
    if (srcTeleportId < 0)
        srcTeleportId = 0;
    if (srcTeleportId > 2)
        srcTeleportId = 0;

    CVort_engine_setCurSound(CK_TELEPORTSND);

    /* source pad resting tile (flash animation skipped) */
    tile = CK_TILES_TELEDIRT;
    if (teleporters[srcTeleportId].isOnSnow)
        tile = CK_TILES_TELESNOW;
    map_data_tiles[ck_rowofs[s_sprite_y] + s_sprite_x] = tile;

    /* move Keen + camera; the BG window can't stream an arbitrary jump,
     * so re-fill it under forced blank */
    k->pos_x = keen_wmap_x_pos = teleporters[dstTeleportId].destX;
    k->pos_y = keen_wmap_y_pos = teleporters[dstTeleportId].destY;
    k->del_x = k->del_y = k->vel_x = k->vel_y = 0;
    scroll_x = k->pos_x - 0x9000L;
    scroll_y = k->pos_y - 0x3000L;
    wm_clamp_scroll();

    tx = (u16)CK_W2T(k->pos_x);
    ty = (u16)CK_W2T(k->pos_y);
    tile = CK_TILES_TELEDIRT;
    if (teleporters[dstTeleportId].isOnSnow)
        tile = CK_TILES_TELESNOW;
    map_data_tiles[ck_rowofs[ty] + tx] = tile;

    setScreenOff();
    wm_sync_camera();
    ck_render_level_init();
    setScreenOn();

    wmap_scroll_x = scroll_x;
    wmap_scroll_y = scroll_y;
    s_wait_release = 1;         /* desktop waits for button release */
    return 1;
}
#endif

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
/* ---- ep3 map sprites: BWB text + shore tiles + teleporters ---------------
 * (CVort3_worldmap_sprites, src/episodes/episode3.c:1603). Teleporter
 * pads are indexed from the sprite plane (0xFst) via the table filled in
 * CVort_mark_cities_done; the flash animation (tiles 130..133) is
 * skipped like ep1's, the 0x86 resting tile is kept. */
static u8 CVort3_worldmap_sprites(u16 v, CkSprite *k)
{
    u16 si, tx, ty;

    if (v == 0x14) { /* the BWB: Keen decides to keep questing */
        ck_ui_message("YOU FEEL LIKE A REST, BUT THE",
                      "GRAND INTELLECT MUST BE FOUND!");
        return 1;
    }
    /* messie water/shore tiles: handled (stay on the map) */
    if ((v & 0xFF00) >= 0x2000) {
        if ((v & 0xFF00) <= 0x2200)
            return 1;
    }
    /* teleporters */
    if ((v & 0xF00) != 0xF00)
        return 0;

    CVort_engine_setCurSound(CK_TELEPORTSND);

    /* source pad resting tile (flash animation skipped) */
    si = (v >> 4) & 0xF;
    map_data_tiles[ck_rowofs[(u16)teleporters[si].destY]
                   + (u16)teleporters[si].destX] = 0x86;

    /* destination pad */
    si = v & 0xF;
    tx = (u16)teleporters[si].destX;
    ty = (u16)teleporters[si].destY;
    if (tx < 10)
        scroll_x = 0x2000;
    else
        scroll_x = CK_T2W(tx - 10);
    if (ty < 6)
        scroll_y = 0x2000;
    else
        scroll_y = CK_T2W(ty - 6);
    map_data_tiles[ck_rowofs[ty] + tx] = 0x86;

    k->pos_x = keen_wmap_x_pos = CK_T2W(tx);
    k->pos_y = keen_wmap_y_pos = CK_T2W(ty);
    k->del_x = k->del_y = k->vel_x = k->vel_y = 0;
    wm_clamp_scroll();

    /* the BG window can't stream an arbitrary jump: re-fill it */
    setScreenOff();
    wm_sync_camera();
    ck_render_level_init();
    setScreenOn();

    wmap_scroll_x = scroll_x;
    wmap_scroll_y = scroll_y;
    s_wait_release = 1;
    return 1;
}

/* ---- Messie (CVort3_do_messie, src/episodes/episode3.c:1715) ------------ */
static void CVort3_do_messie(CkSprite *keen)
{
    static const s16 messie_dir_h[9] = { -2, 0, 2, -2, 0, 2, -2, 0, 2 };
    static const s16 messie_dir_v[9] = { -2, -2, -2, 0, 0, 0, 2, 2, 2 };
    static const u16 messie_frames_t[9] =
        { 0x88, 0x88, 0x86, 0x82, 0x82, 0x84, 0x82, 0x84, 0x84 };
    static const u16 kessie_frames_t[9] =
        { 0x90, 0x90, 0x8E, 0x8A, 0x8A, 0x8C, 0x8A, 0x8C, 0x8C };
    s16 si, di, var2, var4;
    u16 var6;
    u8 var8, skip;

    if (messie_move_tics <= 8) {
        messie_xpos += messie_del_x;
        messie_ypos += messie_del_y;
    }

    /* update the messie frame from the movement direction */
    for (si = 0; si < 9; si++) {
        if ((s16)(messie_dir_h[si] << 8) == messie_del_x) {
            if ((s16)(messie_dir_v[si] << 8) == messie_del_y)
                break;
        }
    }
    if (si > 8)
        si = 8; /* SNES guard for the vanilla OOB read */
    if (messie_mounted)
        messie_frame = kessie_frames_t[si];
    else
        messie_frame = messie_frames_t[si];

    /* pick the next water tile to swim to */
    messie_move_tics--;
    if (!messie_move_tics) {
        messie_move_tics = 8;
        var2 = CK_W2T(messie_xpos);
        var4 = CK_W2T(messie_ypos);
        for (si = (s16)(var2 - 1); var2 + 2 > si; si++) {
            var8 = 0;
            for (di = (s16)(var4 - 1); var4 + 2 > di; di++) {
                var6 = map_data_sprites[ck_rowofs[(u16)di] + (u16)si];
                if (var6 != 0x2100) {
                    if (var6 != 0x2000)
                        continue;
                }
                skip = 0;
                if (si == var2) {
                    if (di == var4)
                        skip = 1;
                }
                if (si == (s16)messie_x_T) {
                    if (di == (s16)messie_y_T)
                        skip = 1;
                }
                if (skip)
                    continue;

                if (si < var2)
                    messie_del_x = -0x200;
                else if (si > var2)
                    messie_del_x = 0x200;
                else
                    messie_del_x = 0;

                if (di < var4)
                    messie_del_y = -0x200;
                else if (di > var4)
                    messie_del_y = 0x200;
                else
                    messie_del_y = 0;

                messie_x_T = (u16)var2;
                messie_y_T = (u16)var4;

                if (var6 == 0x2100) /* nibbling on reeds */
                    messie_move_tics = 130;

                var8++;
                break;
            }
            if (var8)
                break;
        }
    }

    /* handle unmounting + camera follow while mounted */
    if (messie_mounted) {
        static const s16 messie_1_x[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
        static const s16 messie_1_y[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
        u16 dir = ck_input_new.direction;
        /* Vanilla indexes past these arrays when idle (direction == 8);
         * that junk read essentially never matches 0x2200, so the SNES
         * simply skips the check when no direction is held. */
        if (dir < 8) {
            s16 stx = (s16)(CK_W2T(messie_xpos) + messie_1_x[dir]);
            s16 sty = (s16)(CK_W2T(messie_ypos) + messie_1_y[dir]);
            if (map_data_sprites[ck_rowofs[(u16)sty] + (u16)stx] == 0x2200) {
                messie_mounted = 0;
                keen->pos_x = messie_xpos + CK_T2W(messie_1_x[dir]);
                keen->pos_y = messie_ypos + CK_T2W(messie_1_y[dir]);
                keen_wmap_x_pos = keen->pos_x;
                keen_wmap_y_pos = keen->pos_y;
                messie_time_to_climb = 30;
            }
        }

        /* move the screen with the messie */
        if (messie_del_x > 0) {
            if (messie_xpos - scroll_x > 0xB000L) {
                scroll_x += messie_del_x;
                if (scroll_x > scroll_x_max)
                    scroll_x = scroll_x_max;
            }
        } else if (messie_del_x < 0) {
            if (messie_xpos - scroll_x < 0x9000L) {
                scroll_x += messie_del_x;
                if (scroll_x < scroll_x_min)
                    scroll_x = scroll_x_min;
            }
        }
        if (messie_del_y > 0) {
            if (messie_ypos - scroll_y > 0x7000L) {
                scroll_y += messie_del_y;
                if (scroll_y > scroll_y_max)
                    scroll_y = scroll_y_max;
            }
        } else if (messie_del_y < 0) {
            if (messie_ypos - scroll_y < 0x3000L) {
                scroll_y += messie_del_y;
                if (scroll_y < scroll_y_min)
                    scroll_y = scroll_y_min;
            }
        }
        wm_sync_camera();
    }
}
#endif /* episode 3 */

/* ---- CVort_move_worldmap + frame driver ---------------------------------- */
u8 ck_worldmap_frame(void)
{
    CkSprite *k = &g_entities.sprites[0];
    s16 speed, csd;
    u16 fr, v;
    u8 moving, allowTrigger, pressed;

    ck_ticks_lo = (u16)ck_ticks;
    ck_input_update();
    ck_build_game_input();

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    CVort3_do_messie(k);
    if (messie_mounted) {
        /* Keen rides Messie: no walking / level triggers, camera was
         * moved by do_messie. Draw only the (Keen-on-)Messie sprite. */
        scroll_x_tile = CK_W2T(scroll_x);
        scroll_y_tile = CK_W2T(scroll_y);
        ck_msprite_begin();
        ck_msprite_draw((u16)(messie_frame + ((ck_ticks_lo >> 2) & 1)),
                        (s16)((s16)(messie_xpos >> 8) - (s16)ck_cam_px),
                        (s16)((s16)(messie_ypos >> 8) - (s16)ck_cam_py));
        ck_msprite_end();
        ck_render_set_anim_phase((u8)(((ck_ticks_lo >> 3) & 6) >> 1));
        wmap_scroll_x = scroll_x;
        wmap_scroll_y = scroll_y;
        ck_input_old = ck_input_new;
        g_game.wmap_sprite_on = 0;
        return 0;
    }
#endif

    /* swallow a held jump/pogo (level exit / teleport arrival) */
    allowTrigger = 1;
    if (s_wait_release) {
        pressed = 0;
        if (ck_input_new.but1jump)
            pressed = 1;
        if (ck_input_new.but2pogo)
            pressed = 1;
        if (pressed)
            allowTrigger = 0;
        else
            s_wait_release = 0;
    }

    CVort_update_sprite_hitbox_wmap(k);

    /* level/teleporter trigger scan over Keen's hitbox tiles */
    if (allowTrigger) {
        pressed = 0;
        if (ck_input_new.but1jump)
            pressed = 1;
        if (ck_input_new.but2pogo)
            pressed = 1;
        if (pressed) {
            s16 x1 = CK_W2T(k->box_x1), x2 = CK_W2T(k->box_x2);
            s16 y1 = CK_W2T(k->box_y1), y2 = CK_W2T(k->box_y2);
            s16 x, y;
            for (x = x1; x <= x2; x++)
                for (y = y1; y <= y2; y++) {
                    u16 sv = map_data_sprites[ck_rowofs[(u16)y] + (u16)x];
                    if (!sv)
                        continue;
                    s_sprite_x = (u16)x;
                    s_sprite_y = (u16)y;
                    g_game.wmap_sprite_on = sv;
                    if (g_game.wmap_sprite_on == 0xFF)
                        g_game.wmap_sprite_on = 0;
                }
        }
    }

    /* 8-direction movement. SNES adaptation: tick-scaled step
     * (0x100/tick) instead of the DOS 0x400/frame. */
    speed = (s16)(ck_sprite_sync << 8);
    k->del_x = k->del_y = 0;
    switch (ck_input_new.direction) {
        case 7: k->del_x = -speed; k->del_y = -speed;
                k->frame = CK_SPR_MAPKEEN_U; break;
        case 0: k->del_y = -speed; k->frame = CK_SPR_MAPKEEN_U; break;
        case 1: k->del_y = -speed; k->del_x = speed;
                k->frame = CK_SPR_MAPKEEN_U; break;
        case 2: k->del_x = speed;  k->frame = CK_SPR_MAPKEEN_R; break;
        case 3: k->del_x = speed;  k->del_y = speed;
                k->frame = CK_SPR_MAPKEEN_D; break;
        case 4: k->del_y = speed;  k->frame = CK_SPR_MAPKEEN_D; break;
        case 5: k->del_y = speed;  k->del_x = -speed;
                k->frame = CK_SPR_MAPKEEN_D; break;
        case 6: k->del_x = -speed; k->frame = CK_SPR_MAPKEEN_L; break;
        default: break;
    }

    moving = 0;
    if (k->del_x | k->del_y) {
        fr = (u16)((ck_ticks_lo >> 4) & 3);
        moving = 1;
    } else
        fr = 0;

    g_game.wmap_col = 0x8000;
    csd = CVort_check_world_map_col(k);
    k->pos_x += k->del_x;
    k->pos_y += k->del_y;

    if (moving && !((ck_ticks_lo >> 3) & 3)) {
        if (csd)
            CVort_engine_setCurSound(2);
        else
            CVort_engine_setCurSound(1);
    }

    /* viewport follow: same thresholds/clamps as the in-level scroller
     * (worldmap.c's goto chain is CVort_do_scrolling with k = slot 0) */
    CVort_do_scrolling();
    scroll_x_tile = CK_W2T(scroll_x);
    scroll_y_tile = CK_W2T(scroll_y);

    /* draw Keen's map avatar (+ Messie in ep3) */
    ck_msprite_begin();
    ck_msprite_draw((u16)(k->frame + fr),
                    (s16)((s16)(k->pos_x >> 8) - (s16)ck_cam_px),
                    (s16)((s16)(k->pos_y >> 8) - (s16)ck_cam_py));
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    ck_msprite_draw((u16)(messie_frame + ((ck_ticks_lo >> 2) & 1)),
                    (s16)((s16)(messie_xpos >> 8) - (s16)ck_cam_px),
                    (s16)((s16)(messie_ypos >> 8) - (s16)ck_cam_py));
#endif
    ck_msprite_end();

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    /* mount check (desktop CVort_move_worldmap tail): touching Messie
     * climbs on after the dismount cool-down. */
    if (!messie_mounted) {
        if (!messie_time_to_climb) {
            u16 mw = (u16)ck_sprite_frames[0x82].wPx;
            u16 mh = (u16)ck_sprite_frames[0x82].hPx;
            if (k->pos_x >= messie_xpos)
                if (k->pos_x <= messie_xpos + ((s32)mw << 8))
                    if (k->pos_y >= messie_ypos)
                        if (k->pos_y <= messie_ypos + ((s32)mh << 8)) {
                            CVort_engine_setCurSound(0x1A); /* snd_crystal */
                            messie_mounted++;
                            messie_time_to_climb = 30;
                        }
        } else {
            messie_time_to_climb--;
        }
    }
#endif

    /* map tile animation (worldmap anim_speed = 3) */
    ck_render_set_anim_phase((u8)(((ck_ticks_lo >> 3) & 6) >> 1));

    keen_wmap_x_pos = k->pos_x;
    keen_wmap_y_pos = k->pos_y;
    wmap_scroll_x = scroll_x;
    wmap_scroll_y = scroll_y;

    ck_input_old = ck_input_new;

    v = g_game.wmap_sprite_on;
    if (!v)
        return 0;
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    if (CVort1_worldmap_sprites((u16)(v & 0x7FFF), k)) {
        g_game.wmap_sprite_on = 0;
        return 0;
    }
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 3
    if (CVort3_worldmap_sprites((u16)(v & 0x7FFF), k)) {
        g_game.wmap_sprite_on = 0;
        return 0;
    }
#endif
    g_game.wmap_sprite_on = 0;
    v &= 0x7FFF;
    if (v >= 1) {
        if (v <= 16) {
            g_game.on_world_map = 0;
            return (u8)v;
        }
    }
    return 0;
}
