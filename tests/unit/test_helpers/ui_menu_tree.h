#ifndef NT_TEST_HELPER_UI_MENU_TREE_H
#define NT_TEST_HELPER_UI_MENU_TREE_H

/* The menu tree the menu and scenario tests share: File > (New, Open > (Project, File2), Quit), Tools > (Opt),
 * Edit. Keys are unique among siblings; a row id is nt_ui_menu_test_item_id(parent row id or menu id, key). */

#include <stdbool.h>
#include <stdint.h>

#include "ui/nt_ui.h"
#include "ui/nt_ui_menu.h"

#define UI_MT_KEY_FILE 1U
#define UI_MT_KEY_EDIT 2U
#define UI_MT_KEY_NEW 10U
#define UI_MT_KEY_OPEN 11U
#define UI_MT_KEY_QUIT 12U
#define UI_MT_KEY_PROJECT 20U
#define UI_MT_KEY_FILE2 21U
#define UI_MT_KEY_TOOLS 30U
#define UI_MT_KEY_OPT 31U

/* What one frame of the tree declared and returned. */
typedef struct {
    bool file_body;     /* File's submenu body was declared */
    bool open_body;     /* File > Open's */
    bool tools_body;    /* Tools' */
    uint32_t activated; /* the row id of the leaf whose row call returned true (0 = none) */
} ui_menu_tree_t;

static inline void ui_menu_tree_leaf(nt_ui_menu_t *menu, uint32_t scope, uint32_t key, const char *label, ui_menu_tree_t *out) {
    if (nt_ui_menu_item(menu, key, label)) {
        out->activated = nt_ui_menu_test_item_id(scope, key);
    }
}

/* Declares the tree for one frame of the menu opened with nt_ui_menu_open; returns what nt_ui_menu_begin did. */
static inline bool ui_menu_tree(nt_ui_menu_t *menu, nt_ui_context_t *ctx, nt_ui_menu_style_t *style, ui_menu_tree_t *out) {
    *out = (ui_menu_tree_t){0};
    if (!nt_ui_menu_begin(menu, ctx, NULL, 0U, style)) {
        return false;
    }
    const uint32_t file = nt_ui_menu_test_item_id(menu->id, UI_MT_KEY_FILE);
    if (nt_ui_menu_submenu_begin(menu, UI_MT_KEY_FILE, "File")) {
        out->file_body = true;
        ui_menu_tree_leaf(menu, file, UI_MT_KEY_NEW, "New", out);
        if (nt_ui_menu_submenu_begin(menu, UI_MT_KEY_OPEN, "Open")) {
            out->open_body = true;
            const uint32_t open = nt_ui_menu_test_item_id(file, UI_MT_KEY_OPEN);
            ui_menu_tree_leaf(menu, open, UI_MT_KEY_PROJECT, "Project", out);
            ui_menu_tree_leaf(menu, open, UI_MT_KEY_FILE2, "File2", out);
            nt_ui_menu_submenu_end(menu);
        }
        ui_menu_tree_leaf(menu, file, UI_MT_KEY_QUIT, "Quit", out);
        nt_ui_menu_submenu_end(menu);
    }
    if (nt_ui_menu_submenu_begin(menu, UI_MT_KEY_TOOLS, "Tools")) {
        out->tools_body = true;
        ui_menu_tree_leaf(menu, nt_ui_menu_test_item_id(menu->id, UI_MT_KEY_TOOLS), UI_MT_KEY_OPT, "Opt", out);
        nt_ui_menu_submenu_end(menu);
    }
    ui_menu_tree_leaf(menu, menu->id, UI_MT_KEY_EDIT, "Edit", out);
    nt_ui_menu_end(menu);
    return true;
}

#endif /* NT_TEST_HELPER_UI_MENU_TREE_H */
