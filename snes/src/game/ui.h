/* M6 UI: title screen, main menu, text viewer, status box, dialogs.
 * Everything draws on the BG3 text console (engine/text.h); the title
 * art takes over BG1 (the next level load rebuilds BG1 fully).
 */
#ifndef CK_SNES_GAME_UI_H
#define CK_SNES_GAME_UI_H

#include <snes.h>

enum {
    CK_MENU_NEW_GAME = 0,
    CK_MENU_CONTINUE,          /* + ck_ui_menu_slot selected */
    CK_MENU_STORY,
    CK_MENU_BACK               /* picker cancelled: show the menu again */
};

extern u8 ck_ui_menu_slot;     /* save slot chosen for Continue */

/* Title art + "press a button". Returns when a button is pressed. */
void ck_ui_titlescreen(void);

/* Main menu; returns CK_MENU_*. */
u8 ck_ui_menu(void);

/* Save-slot picker: returns 0..8 or 0xFF on back. forSave=1 lists
 * empty slots as choosable. */
u8 ck_ui_slot_picker(u8 forSave);

/* Paged word-wrapped viewer for baked text blobs (0x0D newline,
 * 0x1A terminator). */
void ck_ui_text_viewer(const char *txt);

/* In-level pull-up status box (call when Start is pressed; blocks
 * until closed; gameplay is naturally paused while inside). */
void ck_ui_status_box(void);

/* Small centered message box; blocks until B/Start. */
void ck_ui_message(const char *l1, const char *l2);

/* "Keens Left" box after dying (worldmap return). */
void ck_ui_keens_left(void);

/* Game-over box; blocks, then returns. */
void ck_ui_game_over(void);

#endif
