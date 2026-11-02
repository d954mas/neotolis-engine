#include "file_dialog/nt_file_dialog.h"

#include "core/nt_assert.h"
#include "tinyfiledialogs.h"

#include <string.h>

uint32_t nt_file_dialog_open(const char *title, const char *pattern, char *out, size_t cap) {
    NT_ASSERT(out != NULL && cap > 0);
    const char *const patterns[1] = {pattern};
    /* Several chosen files come back as one string separated by '|'. */
    const char *chosen = tinyfd_openFileDialog(title, NULL, pattern != NULL ? 1 : 0, pattern != NULL ? patterns : NULL, NULL, 1);
    uint32_t count = 0;
    size_t used = 0;
    for (const char *p = chosen; p != NULL && *p != '\0';) {
        const char *bar = strchr(p, '|');
        const size_t n = bar != NULL ? (size_t)(bar - p) : strlen(p);
        if (used + n + 1 > cap) {
            break;
        }
        memcpy(out + used, p, n);
        out[used + n] = '\0';
        used += n + 1;
        count++;
        p += n + (bar != NULL ? 1U : 0U);
    }
    return count;
}
