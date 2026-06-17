#ifndef NT_CLIPBOARD_H
#define NT_CLIPBOARD_H

#include <stdbool.h>

/* Sync façade over the platform clipboard so nt_ui can call it directly in the
 * frame loop (no Promise leaks out — the web backend caches async results). */

/* Returns engine-owned UTF-8 storage valid only until the next clipboard call;
 * callers must copy immediately. Never NULL (empty string when nothing cached). */
const char *nt_clipboard_get_text(void);

/* Copies utf8 to the system clipboard and updates the in-app cache (NULL is treated as "").
 * The in-app cache (get_text) is always updated; the SYSTEM-clipboard write is best-effort on web
 * (navigator.clipboard.writeText needs recent user activation and may silently fail). */
void nt_clipboard_set_text(const char *utf8);

/* True when a real clipboard backend is linked, false for the inert stub. This reports backend
 * PRESENCE (the façade + reliable in-app get/set cache work) -- NOT that a system-clipboard WRITE will
 * succeed (web set_text is best-effort, see above). A LINKED symbol (not a #define): availability is a
 * link-time/runtime fact the once-compiled consumer cannot see via a macro. */
bool nt_clipboard_available(void);

#endif /* NT_CLIPBOARD_H */
