#ifndef TJ_JOURNAL_H
#define TJ_JOURNAL_H

/* Event log ("летопись"): a ring buffer of recent run events. Systems push,
 * the view renders the last few lines (newest at the bottom). Pure data — no
 * rendering or game logic here. */

#define TJ_JOURNAL_MAX 64
#define TJ_JOURNAL_LINE 120

typedef enum {
    TJ_LOG_PLAIN = 0, /* neutral */
    TJ_LOG_GOOD,      /* gain / success */
    TJ_LOG_BAD,       /* loss / fail */
    TJ_LOG_BIG,       /* lap / death / win */
} tj_log_kind_t;

void tj_journal_clear(void);
void tj_journal_push(tj_log_kind_t kind, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int tj_journal_count(void); /* available entries (<= TJ_JOURNAL_MAX) */
/* idx_from_newest: 0 = newest. Returns text (kind via out_kind) or NULL. */
const char *tj_journal_get(int idx_from_newest, tj_log_kind_t *out_kind);

#endif /* TJ_JOURNAL_H */
