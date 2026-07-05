#include "game/ui.h"
#include "game/game_state.h"
#include "engine/text.h"
#include "engine/input.h"
#include "engine/render.h"
#include "engine/msprite.h"
#include "engine/save.h"
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

/* ---- in-level status box ---------------------------------------------------- */

void ck_ui_status_box(void)
{
    char num[12];
    u8 i;

    ck_text_clear();
    ck_text_box(3, 5, 26, 12, CK_TEXT_WHITE);
    ck_text_string(11, 6, "STATUS BOX", CK_TEXT_RED);

    ck_text_string(5, 8, "SCORE", CK_TEXT_WHITE);
    ui_dec((u32)keen_gp.score, num, 9);
    ck_text_string(18, 8, num, CK_TEXT_WHITE);

    ck_text_string(5, 10, "KEENS LEFT", CK_TEXT_WHITE);
    ui_dec((u32)(u16)keen_gp.lives, num, 3);
    ck_text_string(24, 10, num, CK_TEXT_WHITE);

    ck_text_string(5, 12, "AMMO", CK_TEXT_WHITE);
    ui_dec((u32)keen_gp.ammo, num, 3);
    ck_text_string(24, 12, num, CK_TEXT_WHITE);

    /* keycards + per-episode extras as Y/N flags */
    ck_text_string(5, 14, "KEYS", CK_TEXT_WHITE);
    for (i = 0; i < 4; i++)
        ck_text_char((u8)(12 + i), 14,
                     (u8)(keen_gp.stuff[5 + i] ? '*' : '-'), CK_TEXT_RED);
#if CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 1
    ck_text_string(18, 14, "PARTS", CK_TEXT_WHITE);
    ck_text_char(25, 14, (u8)(keen_gp.stuff[0] ? '*' : '-'), CK_TEXT_RED);
    ck_text_char(26, 14, (u8)(keen_gp.stuff[1] ? '*' : '-'), CK_TEXT_RED);
    ck_text_char(27, 14, (u8)(keen_gp.stuff[2] ? '*' : '-'), CK_TEXT_RED);
    ck_text_char(28, 14, (u8)(keen_gp.stuff[4] ? '*' : '-'), CK_TEXT_RED);
#elif CHOCOLATE_KEEN_CONFIG_SPECIFIC_EPISODE == 2
    ck_text_string(18, 14, "SAVED", CK_TEXT_WHITE);
    {
        u8 saved = 0;
        for (i = 0; i < 8; i++)
            if (keen_gp.targets[i])
                saved++;
        ck_text_char(25, 14, (u8)('0' + saved), CK_TEXT_RED);
        ck_text_string(26, 14, "/8", CK_TEXT_RED);
    }
#else
    ck_text_string(18, 14, "ANKH", CK_TEXT_WHITE);
    ui_dec((u32)(u16)(g_game.keen_invincible / 0x90), num, 4);
    ck_text_string(24, 14, num, CK_TEXT_RED);
#endif

    ck_text_show(1);
    ui_wait_key();
    ck_text_show(0);
    ck_text_clear();
}

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
