#include "game/ui.h"
#include "game/game_state.h"
#include "engine/text.h"
#include "engine/input.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/save.h"
#include "engine/levelload.h"
#include "data_format.h"
#include "snes_data_gen.h"

u8 ck_ui_menu_slot;

/* ---- shared helpers ---------------------------------------------------- */

/* UI frame tail for screens with no live gameplay behind them. */
static void ui_frame(void)
{
    WaitForVBlank();
    ck_text_vblank();
}

static void ui_wait_release(void)
{
    while (padsCurrent(0))
        ui_frame();
}

/* Wait for a fresh B/Start press. */
static void ui_wait_key(void)
{
    ui_wait_release();
    for (;;) {
        u16 pad = padsCurrent(0);
        if (pad & (KEY_B | KEY_START | KEY_A))
            break;
        ui_frame();
    }
    ui_wait_release();
}

/* u32 -> right-aligned decimal into buf[width], space padded. No 32-bit
 * divides: repeated subtraction of decimal powers. */
static void ui_dec(u32 v, char *buf, u8 width)
{
    static const u32 pow10[9] = {
        100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL
    };
    char tmp[10];
    u8 i, d, started = 0, n = 0;
    for (i = 0; i < 9; i++) {
        d = 0;
        while (v >= pow10[i]) {
            v -= pow10[i];
            d++;
        }
        if (d || started || i == 8) {
            started = 1;
            tmp[n++] = (char)('0' + d);
        }
    }
    for (i = 0; i < width; i++)
        buf[i] = ' ';
    buf[width] = 0;
    d = (n > width) ? (u8)(n - width) : 0;
    for (i = d; i < n; i++)
        buf[width - n + i] = tmp[i];
}

/* ---- title screen ------------------------------------------------------- */

void ck_ui_titlescreen(void)
{
    u16 i;
    const u16 *map = ck_screen_title.map;

    setScreenOff();
    ck_msprite_begin();
    ck_msprite_end();            /* hide sprites */

    /* BG palette 0: at cold boot nothing has written CGRAM 0..15 yet
     * (ck_render_level_init only runs at level load), and power-on CGRAM
     * is undefined — black on some emulators, garbage on others. */
    dmaCopyCGram((u8 *)ck_pal_ega, 0, 32);

    /* Title chr into the BG1 char area, its 64x32 map into the BG1 map.
     * The next level load rebuilds both, so nothing to preserve. */
    dmaCopyVram((u8 *)ck_screen_title.chr, CK_BG1_CHR_VRAM,
                ck_screen_title.chrLen);
    REG_VMAIN = 0x80;
    REG_VMADDLH = CK_BG1_MAP_VRAM;
    for (i = 0; i < 0x800; i++)
        REG_VMDATALH = map[i];

    /* center the 264x112 title art in the 256x224 window: crop 4 px
     * left/right, 56 px borders top/bottom */
    bgSetScroll(0, 4, (u16)(-56) & 255);
    bgSetEnable(0);

    ck_text_clear();
    ck_text_string(6, 25, "PRESS A BUTTON TO START", CK_TEXT_DARK);
    ck_text_show(1);

    setScreenOn();
    ui_wait_key();
    ck_text_show(0);
}

/* ---- main menu ----------------------------------------------------------- */

#define MENU_ITEMS 3

u8 ck_ui_menu(void)
{
    static const char *items[MENU_ITEMS] = {
        "NEW GAME", "CONTINUE", "THE STORY"
    };
    u8 sel = 0, i;

    setScreenOff();
    bgSetDisable(0);             /* text-only screen */
    ck_text_clear();
    ck_text_box(5, 6, 22, 12, CK_TEXT_WHITE);
    ck_text_string(9, 7, "CHOCOLATE KEEN", CK_TEXT_RED);
    for (i = 0; i < MENU_ITEMS; i++)
        ck_text_string(9, (u8)(10 + (i << 1)), items[i], CK_TEXT_WHITE);
    ck_text_show(1);
    setScreenOn();

    ui_wait_release();
    for (;;) {
        u16 pad;
        for (i = 0; i < MENU_ITEMS; i++)
            ck_text_char(7, (u8)(10 + (i << 1)),
                         (u8)((i == sel) ? '>' : ' '), CK_TEXT_RED);
        ui_frame();

        pad = padsCurrent(0);
        if (pad & KEY_UP) {
            if (sel)
                sel--;
            else
                sel = MENU_ITEMS - 1;
            ui_wait_release();
        } else if (pad & KEY_DOWN) {
            sel++;
            if (sel >= MENU_ITEMS)
                sel = 0;
            ui_wait_release();
        } else if (pad & (KEY_B | KEY_START)) {
            ui_wait_release();
            if (sel == CK_MENU_CONTINUE) {
                u8 pick = ck_ui_slot_picker(0);
                if (pick == 0xFF)
                    return CK_MENU_BACK;
                ck_ui_menu_slot = pick;
            }
            ck_text_show(0);
            return sel;
        }
    }
}

/* ---- save slot picker ------------------------------------------------------
 * Lists all 9 slots with a summary (levels done, lives, score) or
 * "- EMPTY -". Returns the chosen slot, or 0xFF on Y/back. forSave = 1
 * allows choosing empty slots; forSave = 0 (Continue) only used ones. */
u8 ck_ui_slot_picker(u8 forSave)
{
    CkSaveSlot slot;
    u8 used[CK_SAVE_SLOTS];
    u8 i, sel = 0;
    char num[12];

    setScreenOff();
    bgSetDisable(0);
    ck_text_clear();
    ck_text_box(1, 2, 30, 24, CK_TEXT_WHITE);
    ck_text_string(6, 3, forSave ? "SAVE TO SLOT" : "LOAD GAME",
                   CK_TEXT_RED);
    for (i = 0; i < CK_SAVE_SLOTS; i++) {
        u8 row = (u8)(5 + (i << 1));
        used[i] = ck_save_read(i, &slot);
        ck_text_string(4, row, (u8)i == 0 ? "AUTO" : "SLOT", CK_TEXT_WHITE);
        if (i)
            ck_text_char(9, row, (u8)('0' + i), CK_TEXT_WHITE);
        if (used[i]) {
            u8 done = 0, b;
            for (b = 0; b < 16; b++) {
                if (slot.doneLevels & (u16)(1 << b))
                    done++;
            }
            ui_dec((u32)done, num, 2);
            ck_text_string(12, row, "LV", CK_TEXT_GRAY);
            ck_text_string(14, row, num, CK_TEXT_WHITE);
            ui_dec((u32)(u16)slot.lives, num, 2);
            ck_text_char(17, row, 'x', CK_TEXT_GRAY);
            ck_text_string(18, row, num, CK_TEXT_WHITE);
            ui_dec((u32)slot.score, num, 8);
            ck_text_string(21, row, num, CK_TEXT_WHITE);
        } else {
            ck_text_string(14, row, "- EMPTY -", CK_TEXT_GRAY);
        }
    }
    ck_text_string(5, 24, "B: PICK      Y: BACK", CK_TEXT_DARK);
    ck_text_show(1);
    setScreenOn();

    ui_wait_release();
    for (;;) {
        u16 pad;
        for (i = 0; i < CK_SAVE_SLOTS; i++)
            ck_text_char(2, (u8)(5 + (i << 1)),
                         (u8)((i == sel) ? '>' : ' '), CK_TEXT_RED);
        ui_frame();

        pad = padsCurrent(0);
        if (pad & KEY_UP) {
            if (sel)
                sel--;
            else
                sel = CK_SAVE_SLOTS - 1;
            ui_wait_release();
        } else if (pad & KEY_DOWN) {
            sel++;
            if (sel >= CK_SAVE_SLOTS)
                sel = 0;
            ui_wait_release();
        } else if (pad & KEY_Y) {
            ui_wait_release();
            ck_text_show(0);
            ck_text_clear();
            return 0xFF;
        } else if (pad & (KEY_B | KEY_START)) {
            ui_wait_release();
            if (forSave) {
                ck_text_show(0);
                ck_text_clear();
                return sel;
            }
            if (used[sel]) {
                ck_text_show(0);
                ck_text_clear();
                return sel;
            }
            /* Continue on an empty slot: stay in the picker */
        }
    }
}

/* ---- paged text viewer ---------------------------------------------------- */

#define VIEW_COLS 30
#define VIEW_ROWS 20

/* Renders one page starting at *txt; returns the pointer to the next
 * page start (or NULL at the 0x1A terminator). Word wrap at 30 cols. */
static const char *view_page(const char *txt)
{
    u8 row = 0, col;
    ck_text_clear();
    ck_text_box(0, 0, 32, 23, CK_TEXT_WHITE);
    while (row < VIEW_ROWS) {
        col = 0;
        while (*txt != 0x1A) {
            /* measure next word */
            const char *w = txt;
            u8 len = 0;
            if (*w == 0x0D) {
                txt++;
                break;
            }
            if ((u8)*w < 0x20) {   /* stray control byte: as space */
                txt++;
                col++;
                if (col >= VIEW_COLS)
                    break;
                continue;
            }
            if (*w == ' ') {
                txt++;
                col++;
                if (col >= VIEW_COLS)
                    break;
                continue;
            }
            while ((u8)*w > 0x20 || *w == 0x1A) {
                if (*w == 0x1A)
                    break;
                len++;
                w++;
            }
            if (len > VIEW_COLS)
                len = VIEW_COLS;
            if ((u8)(col + len) > VIEW_COLS)
                break;              /* wrap to next row */
            {
                u8 k;
                for (k = 0; k < len; k++)
                    ck_text_char((u8)(1 + col + k), (u8)(1 + row),
                                 (u8)txt[k], CK_TEXT_WHITE);
            }
            txt += len;
            col += len;
        }
        row++;
        if (*txt == 0x1A)
            break;
    }
    ck_text_string(4, 21, "B: MORE   START: DONE", CK_TEXT_DARK);
    return (*txt == 0x1A) ? 0 : txt;
}

void ck_ui_text_viewer(const char *txt)
{
    const char *next;

    setScreenOff();
    bgSetDisable(0);
    ck_text_show(1);
    setScreenOn();

    for (;;) {
        next = view_page(txt);
        ui_wait_key();
        if (!next)
            break;
        txt = next;
        {
            u16 pad = padsCurrent(0);
            if (pad & KEY_START)
                break;
        }
    }
    ck_text_show(0);
}

/* ---- BG1 icon overlays -------------------------------------------------
 * The status box and the ep1 ship dialog show real game tiles (ship
 * parts, keycards) inside their text boxes, like the DOS pause menu
 * (src/episodes/episode1.c CVort1_show_pause_menu). BG3 text sits above
 * BG1, so the icon cells are punched transparent (glyph 0) and the tiles
 * are poked into map_data_tiles, then shown by a forced-blank window
 * refill. BG1 scroll is latched to the 16px grid while the dialog is up
 * so a world tile aligns exactly with a 2x2 text-cell pair; gameplay is
 * paused and the next flow frame re-latches the true camera scroll.
 *
 * A "slot" (c, r) is the world tile at (cam_tile + c, cam_tile + r),
 * displayed exactly at text cells (2c, 2r)..(2c+1, 2r+1).
 */

#define UI_PATCH_TX 5            /* patch region: slots c 5..12, r 6..8 */
#define UI_PATCH_TY 6
#define UI_PATCH_W  8
#define UI_PATCH_H  3
static u16 s_patch[UI_PATCH_W * UI_PATCH_H];
static u16 s_patchTx, s_patchTy;

static void ui_patch_open(void)
{
    u16 c, r;
    u16 *p = s_patch;
    s_patchTx = (u16)((ck_cam_px >> 4) + UI_PATCH_TX);
    s_patchTy = (u16)((ck_cam_py >> 4) + UI_PATCH_TY);
    for (r = 0; r < UI_PATCH_H; r++)
        for (c = 0; c < UI_PATCH_W; c++)
            *p++ = map_data_tiles[ck_rowofs[s_patchTy + r] + s_patchTx + c];
}

/* Poke a game tile into overlay slot (c, r). Must lie in the patch. */
static void ui_slot(u8 c, u8 r, u16 tile)
{
    map_data_tiles[ck_rowofs[(u16)((ck_cam_py >> 4) + r)]
                   + (u16)((ck_cam_px >> 4) + c)] = tile;
}

/* Clear a text-cell rectangle to glyph 0 (transparent: BG1 shows). */
static void ui_punch(u8 x, u8 y, u8 w, u8 h)
{
    u8 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            ck_text_char((u8)(x + i), (u8)(y + j), 0, 0);
}

/* Repaint the BG1 window from the (poked) map and latch grid-aligned
 * scroll for the dialog. */
static void ui_bg1_refill(void)
{
    setScreenOff();
    ck_render_level_init();
    bgSetScroll(0, (u16)(ck_cam_px & 0x1F0),
                (u16)(((ck_cam_py & 0xFFF0) - 1) & 255));
    setScreenOn();
}

/* Restore the patched map region and repaint with true camera scroll. */
static void ui_patch_close(void)
{
    u16 c, r;
    const u16 *p = s_patch;
    for (r = 0; r < UI_PATCH_H; r++)
        for (c = 0; c < UI_PATCH_W; c++)
            map_data_tiles[ck_rowofs[s_patchTy + r] + s_patchTx + c] = *p++;
    setScreenOff();
    ck_render_level_init();
    setScreenOn();
}

/* ---- in-level status box ---------------------------------------------------- */

void ck_ui_status_box(void)
{
    char num[12];
    u8 i;

    ck_text_clear();
    ck_text_box(3, 2, 26, 20, CK_TEXT_WHITE);
    ck_text_string(11, 3, "STATUS BOX", CK_TEXT_RED);

    ck_text_string(5, 5, "SCORE", CK_TEXT_WHITE);
    ui_dec((u32)keen_gp.score, num, 9);
    ck_text_string(17, 5, num, CK_TEXT_WHITE);

    ck_text_string(5, 7, "KEENS LEFT", CK_TEXT_WHITE);
    ui_dec((u32)(u16)keen_gp.lives, num, 3);
    ck_text_string(23, 7, num, CK_TEXT_WHITE);

    ck_text_string(5, 9, "AMMO", CK_TEXT_WHITE);
    ui_dec((u32)keen_gp.ammo, num, 3);
    ck_text_string(23, 9, num, CK_TEXT_WHITE);

    /* keycards + per-episode extras as real game tiles on BG1: icon
     * rows are slots r=6 (cells 12-13) and r=8 (cells 16-17); unused
     * slots show the empty tile 0x8F. */
    ck_text_string(5, 12, "KEYS", CK_TEXT_WHITE);
    ui_punch(10, 12, 16, 2);

    ui_patch_open();
    for (i = 0; i < UI_PATCH_W; i++)
        ui_slot((u8)(UI_PATCH_TX + i), 6, 0x8F);

#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    for (i = 0; i < 4; i++)
        if (keen_gp.stuff[5 + i])
            ui_slot((u8)(6 + (i << 1)), 6, (u16)(0x1A8 + i));

    /* ship parts, DOS order: joystick, battery, vacuum, everclear;
     * missing art 0x141.., collected art 0x1C0.. */
    ck_text_string(5, 16, "PARTS", CK_TEXT_WHITE);
    ui_punch(10, 16, 16, 2);
    for (i = 0; i < UI_PATCH_W; i++)
        ui_slot((u8)(UI_PATCH_TX + i), 8, 0x8F);
    ui_slot(6, 8, keen_gp.stuff[0] ? 0x1C0 : 0x141);
    ui_slot(8, 8, keen_gp.stuff[4] ? 0x1C1 : 0x142);
    ui_slot(10, 8, keen_gp.stuff[1] ? 0x1C2 : 0x143);
    ui_slot(12, 8, keen_gp.stuff[2] ? 0x1C3 : 0x144);
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    for (i = 0; i < 4; i++)
        if (keen_gp.stuff[5 + i])
            ui_slot((u8)(6 + (i << 1)), 6, (u16)(0x1A8 + i));

    ck_text_string(5, 16, "SAVED", CK_TEXT_WHITE);
    {
        u8 saved = 0;
        for (i = 0; i < 8; i++)
            if (keen_gp.targets[i])
                saved++;
        ck_text_char(12, 16, (u8)('0' + saved), CK_TEXT_RED);
        ck_text_string(13, 16, "/8", CK_TEXT_RED);
    }
#else
    for (i = 0; i < 4; i++)
        if (keen_gp.stuff[5 + i])
            ui_slot((u8)(6 + (i << 1)), 6, (u16)(0xD9 + i));

    /* ankh icon (0xD6, drawn unconditionally like the DOS menu) + time */
    ck_text_string(5, 16, "ANKH", CK_TEXT_WHITE);
    ui_punch(12, 16, 2, 2);
    ui_slot(6, 8, 0xD6);
    ui_dec((u32)(u16)(g_game.keen_invincible / 0x90), num, 4);
    ck_text_string(15, 16, num, CK_TEXT_RED);
#endif

    ui_bg1_refill();
    ck_text_show(1);
    ui_wait_key();
    ck_text_show(0);
    ck_text_clear();
    ui_patch_close();
}

/* ---- ep1 ship parts dialog (world map) -------------------------------------
 * DOS CVort1_worldmap_sprites: standing on the BWB lists the parts still
 * missing (tiles 0x141..0x144) in a box. Uses the same BG1 overlay
 * machinery as the status box; caller is the map session (render live). */
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
void ck_ui_ship_parts(void)
{
    ck_text_clear();
    ck_text_box(5, 6, 22, 11, CK_TEXT_WHITE);
    ck_text_string(7, 8, "YOUR SHIP IS MISSING", CK_TEXT_WHITE);
    ck_text_string(7, 9, "THESE PARTS:", CK_TEXT_WHITE);
    ck_text_string(9, 15, "GO GET THEM!", CK_TEXT_RED);
    ui_punch(10, 12, 16, 2);

    ui_patch_open();
    {
        u8 i;
        for (i = 0; i < UI_PATCH_W; i++)
            ui_slot((u8)(UI_PATCH_TX + i), 6, 0x8F);
    }
    if (!keen_gp.stuff[0])
        ui_slot(6, 6, 0x141);   /* joystick  */
    if (!keen_gp.stuff[4])
        ui_slot(8, 6, 0x142);   /* battery   */
    if (!keen_gp.stuff[1])
        ui_slot(10, 6, 0x143);  /* vacuum    */
    if (!keen_gp.stuff[2])
        ui_slot(12, 6, 0x144);  /* everclear */

    ui_bg1_refill();
    ck_text_show(1);
    ui_wait_key();
    ck_text_show(0);
    ck_text_clear();
    ui_patch_close();
}
#endif

/* ---- small dialogs ------------------------------------------------------------ */

void ck_ui_message(const char *l1, const char *l2)
{
    u8 w = 0, n;
    const char *p;
    for (p = l1, n = 0; *p; p++, n++) ;
    w = n;
    if (l2) {
        for (p = l2, n = 0; *p; p++, n++) ;
        if (n > w)
            w = n;
    }
    w += 4;
    ck_text_clear();
    ck_text_box((u8)((32 - w) >> 1), 9, w, (u8)(l2 ? 7 : 5), CK_TEXT_WHITE);
    ck_text_string((u8)((32 - w) / 2 + 2), 11, l1, CK_TEXT_WHITE);
    if (l2)
        ck_text_string((u8)((32 - w) / 2 + 2), 13, l2, CK_TEXT_WHITE);
    ck_text_show(1);
    ui_wait_key();
    ck_text_show(0);
    ck_text_clear();
}

void ck_ui_keens_left(void)
{
    char num[4];
    ui_dec((u32)(u16)keen_gp.lives, num, 2);
    ck_text_clear();
    ck_text_box(8, 9, 16, 5, CK_TEXT_WHITE);
    ck_text_string(10, 11, "KEENS LEFT", CK_TEXT_WHITE);
    ck_text_string(21, 11, num, CK_TEXT_RED);
    ck_text_show(1);
    ui_wait_key();
    ck_text_show(0);
    ck_text_clear();
}

void ck_ui_game_over(void)
{
    ck_ui_message("GAME OVER", 0);
}
