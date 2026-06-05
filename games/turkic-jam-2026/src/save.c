#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TJ_SAVE_KEY "turkic_jam_save"
#define TJ_SAVE_FILE "turkic_jam_save.txt"
#define TJ_SAVE_MAX_ENTRIES 32
#define TJ_SAVE_KEY_LEN 32
#define TJ_SAVE_VAL_LEN 96
#define TJ_SAVE_BLOB_LEN 4096

typedef struct {
    char key[TJ_SAVE_KEY_LEN];
    char val[TJ_SAVE_VAL_LEN];
} tj_kv_t;

static tj_kv_t s_kv[TJ_SAVE_MAX_ENTRIES];
static int s_count;

// #region backend (web localStorage / native file)
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* EM_JS bodies are JavaScript: the formatter mangles them (splits === into == =)
 * and the trailing ';' trips -Wextra-semi. Guard both around the EM_JS block. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
/* clang-format off */
EM_JS(void, tj_js_store, (const char *key, const char *val), {
    try {
        localStorage.setItem(UTF8ToString(key), UTF8ToString(val));
    } catch (e) {
    }
});

/* Returns a malloc'd C string the caller must free(), or 0 if absent. */
EM_JS(char *, tj_js_load, (const char *key), {
    var s = null;
    try {
        s = localStorage.getItem(UTF8ToString(key));
    } catch (e) {
    }
    if (s === null) {
        return 0;
    }
    var len = lengthBytesUTF8(s) + 1;
    var p = _malloc(len);
    stringToUTF8(s, p, len);
    return p;
});
#pragma clang diagnostic pop

static void backend_write(const char *blob) { tj_js_store(TJ_SAVE_KEY, blob); }

static void backend_read(char *out, size_t out_size) {
    out[0] = '\0';
    char *s = tj_js_load(TJ_SAVE_KEY);
    if (s) {
        (void)snprintf(out, out_size, "%s", s);
        free(s);
    }
}
/* clang-format on */
#else
static void backend_write(const char *blob) {
    FILE *f = fopen(TJ_SAVE_FILE, "wb");
    if (f) {
        (void)fwrite(blob, 1, strlen(blob), f);
        (void)fclose(f);
    }
}

static void backend_read(char *out, size_t out_size) {
    out[0] = '\0';
    FILE *f = fopen(TJ_SAVE_FILE, "rb");
    if (f) {
        size_t n = fread(out, 1, out_size - 1, f);
        out[n] = '\0';
        (void)fclose(f);
    }
}
#endif
// #endregion

static int find(const char *key) {
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_kv[i].key, key, TJ_SAVE_KEY_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static void parse(const char *blob) {
    s_count = 0;
    const char *p = blob;
    while (*p && s_count < TJ_SAVE_MAX_ENTRIES) {
        const char *eq = strchr(p, '=');
        const char *nl = strchr(p, '\n');
        if (!eq || (nl && eq > nl)) {
            break;
        }
        size_t klen = (size_t)(eq - p);
        const char *vstart = eq + 1;
        size_t vlen = nl ? (size_t)(nl - vstart) : strlen(vstart);
        if (klen < TJ_SAVE_KEY_LEN && vlen < TJ_SAVE_VAL_LEN) {
            memcpy(s_kv[s_count].key, p, klen);
            s_kv[s_count].key[klen] = '\0';
            memcpy(s_kv[s_count].val, vstart, vlen);
            s_kv[s_count].val[vlen] = '\0';
            s_count++;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
}

void save_init(void) {
    static char blob[TJ_SAVE_BLOB_LEN];
    backend_read(blob, sizeof blob);
    parse(blob);
}

void save_flush(void) {
    static char blob[TJ_SAVE_BLOB_LEN];
    size_t off = 0;
    for (int i = 0; i < s_count; i++) {
        int n = snprintf(blob + off, sizeof(blob) - off, "%s=%s\n", s_kv[i].key, s_kv[i].val);
        if (n < 0 || (size_t)n >= sizeof(blob) - off) {
            break;
        }
        off += (size_t)n;
    }
    backend_write(blob);
}

void save_set_str(const char *key, const char *value) {
    int i = find(key);
    if (i < 0) {
        if (s_count >= TJ_SAVE_MAX_ENTRIES) {
            return;
        }
        i = s_count++;
        (void)snprintf(s_kv[i].key, TJ_SAVE_KEY_LEN, "%s", key);
    }
    (void)snprintf(s_kv[i].val, TJ_SAVE_VAL_LEN, "%s", value);
}

const char *save_get_str(const char *key, const char *fallback) {
    int i = find(key);
    return (i < 0) ? fallback : s_kv[i].val;
}

void save_set_int(const char *key, int value) {
    char buf[16];
    (void)snprintf(buf, sizeof buf, "%d", value);
    save_set_str(key, buf);
}

int save_get_int(const char *key, int fallback) {
    int i = find(key);
    return (i < 0) ? fallback : (int)strtol(s_kv[i].val, NULL, 10);
}
