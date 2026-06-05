#include "journal.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct {
    char text[TJ_JOURNAL_LINE];
    tj_log_kind_t kind;
} entry_t;

static entry_t s_buf[TJ_JOURNAL_MAX];
static int s_total; /* total ever pushed; ring index = (s_total-1) % MAX */

void tj_journal_clear(void) { s_total = 0; }

void tj_journal_push(tj_log_kind_t kind, const char *fmt, ...) {
    entry_t *e = &s_buf[s_total % TJ_JOURNAL_MAX];
    va_list a;
    va_start(a, fmt);
    (void)vsnprintf(e->text, sizeof e->text, fmt, a);
    va_end(a);
    e->kind = kind;
    s_total++;
}

int tj_journal_count(void) { return s_total < TJ_JOURNAL_MAX ? s_total : TJ_JOURNAL_MAX; }

const char *tj_journal_get(int idx_from_newest, tj_log_kind_t *out_kind) {
    const int avail = tj_journal_count();
    if (idx_from_newest < 0 || idx_from_newest >= avail) {
        return NULL;
    }
    const int absolute = s_total - 1 - idx_from_newest;
    const entry_t *e = &s_buf[absolute % TJ_JOURNAL_MAX];
    if (out_kind) {
        *out_kind = e->kind;
    }
    return e->text;
}
