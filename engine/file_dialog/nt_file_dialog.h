#ifndef NT_FILE_DIALOG_H
#define NT_FILE_DIALOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#error "nt_file_dialog is native-only: the browser has no filesystem"
#endif

/* Shows the system's open-file dialog and blocks until the user closes it. Writes the chosen paths
 * to out back to back, each NUL-terminated, and returns how many fit in cap bytes; 0 on cancel.
 * pattern filters the listed files ("*.txt"); NULL lists all. On Linux the dialog comes from zenity,
 * kdialog or a similar helper; without one it returns 0. */
uint32_t nt_file_dialog_open(const char *title, const char *pattern, char *out, size_t cap);

#endif /* NT_FILE_DIALOG_H */
