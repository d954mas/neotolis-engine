#ifndef TJ_SAVE_H
#define TJ_SAVE_H

/* Tiny key/value persistence. One backing blob:
 *   - Web:    localStorage["turkic_jam_save"] (survives reload, per-origin).
 *   - Native: turkic_jam_save.txt in the working directory.
 * Values are kept in memory; call save_flush() to persist. Keys/values are
 * short ASCII tokens (no '=' or newline). */

void save_init(void);  /* load backing store into memory */
void save_flush(void); /* persist memory to backing store */

void save_set_int(const char *key, int value);
int save_get_int(const char *key, int fallback);

void save_set_str(const char *key, const char *value);
const char *save_get_str(const char *key, const char *fallback);

#endif /* TJ_SAVE_H */
