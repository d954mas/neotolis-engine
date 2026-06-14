#ifndef NT_CLIPBOARD_H
#define NT_CLIPBOARD_H

/* Sync façade over the platform clipboard so nt_ui can call it directly in the
 * frame loop (no Promise leaks out — the web backend caches async results). */

/* Returns engine-owned UTF-8 storage valid only until the next clipboard call;
 * callers must copy immediately. Never NULL (empty string when nothing cached). */
const char *nt_clipboard_get_text(void);

/* Copies utf8 to the system clipboard (NULL is treated as ""). */
void nt_clipboard_set_text(const char *utf8);

#endif /* NT_CLIPBOARD_H */
