#include "devapi/nt_devapi.h"

#if NT_DEVAPI_ENABLED

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comp_storage/nt_comp_storage.h"
#include "drawable_comp/nt_drawable_comp.h"
#include "entity/nt_entity.h"
#include "input/nt_input.h"
#include "input/nt_input_internal.h"
#include "sprite_comp/nt_sprite_comp.h"
#include "transform_comp/nt_transform_comp.h"
#include "ui/nt_ui_internal.h"

// #region registry + state
#define TJ_MAX_ENDPOINTS 32

typedef struct {
    const char *name;
    nt_devapi_handler_fn fn;
    void *user;
} endpoint_t;

static endpoint_t s_eps[TJ_MAX_ENDPOINTS];
static int s_ep_count;
static const nt_ui_context_t *s_ui_ctx;
static float s_fb_w = 800.0F;
static float s_fb_h = 600.0F;
static float s_log_w = 800.0F;
static float s_log_h = 600.0F;

void nt_devapi_set_ui_context(const nt_ui_context_t *ctx) { s_ui_ctx = ctx; }

void nt_devapi_set_view(float fb_w, float fb_h, float logical_w, float logical_h) {
    s_fb_w = fb_w;
    s_fb_h = fb_h;
    s_log_w = logical_w;
    s_log_h = logical_h;
}

bool nt_devapi_register(const char *name, nt_devapi_handler_fn fn, void *user) {
    if (s_ep_count >= TJ_MAX_ENDPOINTS) {
        return false;
    }
    s_eps[s_ep_count].name = name;
    s_eps[s_ep_count].fn = fn;
    s_eps[s_ep_count].user = user;
    s_ep_count++;
    return true;
}
// #endregion

// #region json writer
typedef struct {
    char *buf;
    int cap;
    int len;
} jw_t;

static void jw_putc(jw_t *w, char c) {
    if (w->len < w->cap - 1) {
        w->buf[w->len++] = c;
    }
}

static void jw_raw(jw_t *w, const char *s) {
    while (*s) {
        jw_putc(w, *s++);
    }
}

static void jw_strn(jw_t *w, const char *s, int n) {
    jw_putc(w, '"');
    for (int i = 0; i < n; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            jw_putc(w, '\\');
            jw_putc(w, c);
        } else if ((unsigned char)c < 0x20) {
            jw_putc(w, ' ');
        } else {
            jw_putc(w, c);
        }
    }
    jw_putc(w, '"');
}

static void jw_str(jw_t *w, const char *s) { jw_strn(w, s, s ? (int)strlen(s) : 0); }

static void __attribute__((format(printf, 2, 3))) jw_fmt(jw_t *w, const char *fmt, ...) {
    int rem = w->cap - w->len;
    if (rem <= 1) {
        return;
    }
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(w->buf + w->len, (size_t)rem, fmt, a);
    va_end(a);
    if (n > 0) {
        w->len += (n < rem) ? n : (rem - 1);
    }
}
// #endregion

// #region arg parsing (console-style key=value)
static const char *arg_str(int argc, char **argv, const char *key, const char *def) {
    size_t kl = strlen(key);
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], key, kl) == 0 && argv[i][kl] == '=') {
            return argv[i] + kl + 1;
        }
    }
    return def;
}

static float arg_f(int argc, char **argv, const char *key, float def) {
    const char *v = arg_str(argc, argv, key, NULL);
    return v ? (float)strtod(v, NULL) : def;
}

static long arg_l(int argc, char **argv, const char *key, long def) {
    const char *v = arg_str(argc, argv, key, NULL);
    return v ? strtol(v, NULL, 0) : def;
}
// #endregion

// #region input queue
static bool s_held[NT_KEY_COUNT];
static uint8_t s_tap[NT_KEY_COUNT];
static float s_mx;
static float s_my;
static uint8_t s_btn_mask;
static uint8_t s_click; /* 0 none, 1 press-frame, 2 release-frame */
static uint8_t s_click_btn;
static bool s_ptr_engaged;
static bool s_ptr_active;

static int ci_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = (*a >= 'a' && *a <= 'z') ? (*a - ('a' - 'A')) : *a;
        int cb = (*b >= 'a' && *b <= 'z') ? (*b - ('a' - 'A')) : *b;
        if (ca != cb) {
            return 0;
        }
    }
    return (*a == '\0') && (*b == '\0');
}

static nt_key_t key_from_name(const char *s) {
    if (!s || !s[0]) {
        return NT_KEY_COUNT;
    }
    if (s[1] == '\0') {
        char ch = s[0];
        if (ch >= 'a' && ch <= 'z') {
            return (nt_key_t)(NT_KEY_A + (ch - 'a'));
        }
        if (ch >= 'A' && ch <= 'Z') {
            return (nt_key_t)(NT_KEY_A + (ch - 'A'));
        }
        if (ch >= '0' && ch <= '9') {
            return (nt_key_t)(NT_KEY_0 + (ch - '0'));
        }
    }

    static const struct {
        const char *n;
        nt_key_t k;
    } tbl[] = {
        {"SPACE", NT_KEY_SPACE},         {"ENTER", NT_KEY_ENTER}, {"ESCAPE", NT_KEY_ESCAPE},   {"ESC", NT_KEY_ESCAPE},      {"TAB", NT_KEY_TAB},
        {"BACKSPACE", NT_KEY_BACKSPACE}, {"UP", NT_KEY_ARROW_UP}, {"DOWN", NT_KEY_ARROW_DOWN}, {"LEFT", NT_KEY_ARROW_LEFT}, {"RIGHT", NT_KEY_ARROW_RIGHT},
        {"DELETE", NT_KEY_DELETE},       {"HOME", NT_KEY_HOME},   {"END", NT_KEY_END},         {"PAGEUP", NT_KEY_PAGE_UP},  {"PAGEDOWN", NT_KEY_PAGE_DOWN},
    };
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++) {
        if (ci_eq(s, tbl[i].n)) {
            return tbl[i].k;
        }
    }
    return NT_KEY_COUNT;
}

static uint8_t button_from_name(const char *s) {
    if (s && (s[0] == 'r' || s[0] == 'R')) {
        return (uint8_t)NT_BUTTON_RIGHT;
    }
    if (s && (s[0] == 'm' || s[0] == 'M')) {
        return (uint8_t)NT_BUTTON_MIDDLE;
    }
    return (uint8_t)NT_BUTTON_LEFT;
}

void nt_devapi_apply_pending(void) {
    for (int k = 0; k < NT_KEY_COUNT; k++) {
        if (s_tap[k] == 1) {
            nt_input_set_key((nt_key_t)k, true);
            s_tap[k] = 2;
        } else if (s_tap[k] == 2) {
            nt_input_set_key((nt_key_t)k, false);
            s_tap[k] = 0;
        } else if (s_held[k]) {
            nt_input_set_key((nt_key_t)k, true);
        }
    }
    if (!s_ptr_engaged) {
        return;
    }
    uint8_t mask = s_btn_mask;
    if (s_click == 1) {
        mask |= (uint8_t)(1U << s_click_btn);
    }
    if (!s_ptr_active) {
        nt_input_pointer_down(0, s_mx, s_my, 1.0F, (uint8_t)NT_POINTER_MOUSE, mask);
        s_ptr_active = true;
    } else {
        nt_input_pointer_move(0, s_mx, s_my, 1.0F, (uint8_t)NT_POINTER_MOUSE, mask);
    }
    if (s_click == 1) {
        s_click = 2;
    } else if (s_click == 2) {
        s_click = 0;
    }
}
// #endregion

// #region builtin endpoints
// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_ping(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)o;
    (void)cap;
    (void)u;
    return 0;
}

static int ep_endpoints(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    jw_t w = {o, cap, 0};
    jw_putc(&w, '[');
    for (int i = 0; i < s_ep_count; i++) {
        if (i) {
            jw_putc(&w, ',');
        }
        jw_str(&w, s_eps[i].name);
    }
    jw_putc(&w, ']');
    return w.len;
}

static int ep_view(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    jw_t w = {o, cap, 0};
    jw_fmt(&w, "{\"fb_w\":%.0f,\"fb_h\":%.0f,\"logical_w\":%.1f,\"logical_h\":%.1f}", (double)s_fb_w, (double)s_fb_h, (double)s_log_w, (double)s_log_h);
    return w.len;
}

static int ep_ui_tree(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    if (!s_ui_ctx) {
        return -1;
    }
    static nt_ui_inspector_tree_row_t rows[256];
    int32_t n = nt_ui_internal_collect_tree_rows(s_ui_ctx, rows, 256);
    jw_t w = {o, cap, 0};
    jw_putc(&w, '[');
    for (int32_t i = 0; i < n; i++) {
        const nt_ui_inspector_tree_row_t *r = &rows[i];
        if (i) {
            jw_putc(&w, ',');
        }
        jw_fmt(&w, "{\"id\":%u,\"depth\":%u,", (unsigned)r->id, (unsigned)r->depth);
        jw_raw(&w, "\"name\":");
        jw_strn(&w, r->id_string ? r->id_string : "", (int)r->id_string_len);
        if (r->is_text && r->text_chars) {
            jw_raw(&w, ",\"text\":");
            jw_strn(&w, r->text_chars, (int)r->text_len);
        }
        jw_fmt(&w, ",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}", (double)r->bbox_x, (double)r->bbox_y, (double)r->bbox_w, (double)r->bbox_h);
    }
    jw_putc(&w, ']');
    return w.len;
}

static int ep_ui_element(int c, char **v, char *o, int cap, void *u) {
    (void)u;
    if (!s_ui_ctx) {
        return -1;
    }
    uint32_t id = (uint32_t)arg_l(c, v, "id", 0);
    nt_ui_inspector_element_info_t e = nt_ui_internal_get_element_info(s_ui_ctx, id);
    if (!e.found) {
        return -1;
    }
    jw_t w = {o, cap, 0};
    jw_fmt(&w, "{\"id\":%u,\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f", (unsigned)id, (double)e.bbox_x, (double)e.bbox_y, (double)e.bbox_w, (double)e.bbox_h);
    jw_fmt(&w, ",\"bg\":[%.0f,%.0f,%.0f,%.0f]", (double)e.bg_r, (double)e.bg_g, (double)e.bg_b, (double)e.bg_a);
    jw_fmt(&w, ",\"font_size\":%u,\"text_align\":%u}", (unsigned)e.text_font_size, (unsigned)e.text_align);
    return w.len;
}

static int ep_entity_list(int c, char **v, char *o, int cap, void *u) {
    (void)c;
    (void)v;
    (void)u;
    nt_transform_comp_view_t t = nt_transform_comp_view();
    nt_sprite_comp_view_t s = nt_sprite_comp_view();
    nt_drawable_comp_view_t d = nt_drawable_comp_view();
    uint16_t max = nt_entity_max();
    jw_t w = {o, cap, 0};
    jw_putc(&w, '[');
    bool first = true;
    for (uint16_t e = 0; e < max; e++) {
        uint16_t ti = t.sparse_indices ? t.sparse_indices[e] : NT_INVALID_COMP_INDEX;
        uint16_t si = s.sparse_indices ? s.sparse_indices[e] : NT_INVALID_COMP_INDEX;
        uint16_t di = d.sparse_indices ? d.sparse_indices[e] : NT_INVALID_COMP_INDEX;
        if (ti == NT_INVALID_COMP_INDEX && si == NT_INVALID_COMP_INDEX && di == NT_INVALID_COMP_INDEX) {
            continue;
        }
        if (!first) {
            jw_putc(&w, ',');
        }
        first = false;
        jw_fmt(&w, "{\"entity\":%u", (unsigned)e);
        if (ti != NT_INVALID_COMP_INDEX) {
            const float *m = t.world_matrices[ti];
            jw_fmt(&w, ",\"x\":%.2f,\"y\":%.2f", (double)m[12], (double)m[13]);
        }
        if (si != NT_INVALID_COMP_INDEX) {
            jw_fmt(&w, ",\"sprite_region\":%u", (unsigned)s.region_index[si]);
        }
        if (di != NT_INVALID_COMP_INDEX) {
            jw_fmt(&w, ",\"color\":%u,\"visible\":%s", (unsigned)d.colors_packed[di], d.visible[di] ? "true" : "false");
        }
        jw_putc(&w, '}');
    }
    jw_putc(&w, ']');
    return w.len;
}

// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_input_key(int c, char **v, char *o, int cap, void *u) {
    (void)o;
    (void)cap;
    (void)u;
    nt_key_t k = key_from_name(arg_str(c, v, "key", ""));
    if (k >= NT_KEY_COUNT) {
        return -1;
    }
    const char *mode = arg_str(c, v, "mode", "tap");
    if (strcmp(mode, "down") == 0) {
        s_held[k] = true;
    } else if (strcmp(mode, "up") == 0) {
        s_held[k] = false;
        s_tap[k] = 2;
    } else {
        s_tap[k] = 1; /* tap */
    }
    return 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_input_move(int c, char **v, char *o, int cap, void *u) {
    (void)o;
    (void)cap;
    (void)u;
    s_mx = arg_f(c, v, "x", s_mx);
    s_my = arg_f(c, v, "y", s_my);
    s_ptr_engaged = true;
    return 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_input_click(int c, char **v, char *o, int cap, void *u) {
    (void)o;
    (void)cap;
    (void)u;
    s_mx = arg_f(c, v, "x", s_mx);
    s_my = arg_f(c, v, "y", s_my);
    s_click_btn = button_from_name(arg_str(c, v, "button", "left"));
    s_click = 1;
    s_ptr_engaged = true;
    return 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_input_button(int c, char **v, char *o, int cap, void *u) {
    (void)o;
    (void)cap;
    (void)u;
    uint8_t btn = button_from_name(arg_str(c, v, "button", "left"));
    bool down = strcmp(arg_str(c, v, "state", "down"), "up") != 0;
    uint8_t bit = (uint8_t)(1U << btn);
    if (down) {
        s_btn_mask |= bit;
    } else {
        s_btn_mask &= (uint8_t)~bit;
    }
    s_ptr_engaged = true;
    return 0;
}

// NOLINTNEXTLINE(readability-non-const-parameter) -- fixed nt_devapi_handler_fn ABI
static int ep_input_click_ui(int c, char **v, char *o, int cap, void *u) {
    (void)o;
    (void)cap;
    (void)u;
    if (!s_ui_ctx) {
        return -1;
    }
    uint32_t id = (uint32_t)arg_l(c, v, "id", 0);
    nt_ui_inspector_element_info_t e = nt_ui_internal_get_element_info(s_ui_ctx, id);
    if (!e.found || s_log_w <= 0.0F || s_log_h <= 0.0F) {
        return -1;
    }
    float cx = e.bbox_x + (e.bbox_w * 0.5F);
    float cy = e.bbox_y + (e.bbox_h * 0.5F);
    s_mx = cx * (s_fb_w / s_log_w);
    s_my = cy * (s_fb_h / s_log_h);
    s_click_btn = (uint8_t)NT_BUTTON_LEFT;
    s_click = 1;
    s_ptr_engaged = true;
    return 0;
}

void nt_devapi_register_builtins(void) {
    nt_devapi_register("ping", ep_ping, NULL);
    nt_devapi_register("endpoints", ep_endpoints, NULL);
    nt_devapi_register("view", ep_view, NULL);
    nt_devapi_register("ui.tree", ep_ui_tree, NULL);
    nt_devapi_register("ui.element", ep_ui_element, NULL);
    nt_devapi_register("entity.list", ep_entity_list, NULL);
    nt_devapi_register("input.key", ep_input_key, NULL);
    nt_devapi_register("input.move", ep_input_move, NULL);
    nt_devapi_register("input.click", ep_input_click, NULL);
    nt_devapi_register("input.click_ui", ep_input_click_ui, NULL);
    nt_devapi_register("input.button", ep_input_button, NULL);
}
// #endregion

// #region dispatch
static endpoint_t *find_ep(const char *name) {
    for (int i = 0; i < s_ep_count; i++) {
        if (strcmp(s_eps[i].name, name) == 0) {
            return &s_eps[i];
        }
    }
    return NULL;
}

int nt_devapi_dispatch(const char *line, char *out, int out_cap) {
    static char buf[1024];
    int bl = 0;
    for (const char *p = line; *p && *p != '\n' && *p != '\r' && bl < (int)sizeof(buf) - 1; p++) {
        buf[bl++] = *p;
    }
    buf[bl] = '\0';

    char *argv[16];
    int argc = 0;
    char *p = buf;
    while (*p && argc < 16) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (!*p) {
            break;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    jw_t w = {out, out_cap, 0};
    if (argc == 0) {
        jw_raw(&w, "{\"ok\":false,\"error\":\"empty\"}");
        return w.len;
    }
    endpoint_t *ep = find_ep(argv[0]);
    if (!ep) {
        jw_raw(&w, "{\"ok\":false,\"error\":\"unknown endpoint\"}");
        return w.len;
    }

    static char data[1 << 16];
    int dn = ep->fn(argc - 1, argv + 1, data, (int)sizeof(data), ep->user);
    if (dn < 0) {
        jw_raw(&w, "{\"ok\":false,\"error\":\"handler\"}");
        return w.len;
    }
    if (dn == 0) {
        jw_raw(&w, "{\"ok\":true}");
        return w.len;
    }
    jw_raw(&w, "{\"ok\":true,\"data\":");
    for (int i = 0; i < dn && w.len < w.cap - 1; i++) {
        w.buf[w.len++] = data[i];
    }
    jw_putc(&w, '}');
    return w.len;
}

void nt_devapi_init(void) {
    s_ep_count = 0;
    s_ui_ctx = NULL;
    memset(s_held, 0, sizeof s_held);
    memset(s_tap, 0, sizeof s_tap);
    s_btn_mask = 0;
    s_click = 0;
    s_ptr_engaged = false;
    s_ptr_active = false;
}

void nt_devapi_shutdown(void) { s_ep_count = 0; }
// #endregion

#endif /* NT_DEVAPI_ENABLED */
