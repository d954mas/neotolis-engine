#ifndef TJ_I18N_H
#define TJ_I18N_H

/* Minimal localization: a key enum indexes a [lang][key] string table.
 * Strings are static UTF-8; safe to hand straight to Clay (outlive the frame). */

typedef enum {
    LANG_EN = 0,
    LANG_RU,
    LANG_TR,
    LANG_COUNT,
} i18n_lang_t;

typedef enum {
    T_TITLE = 0,
    T_SUBTITLE,
    T_START,
    T_LANGUAGE,
    T_GAME_TITLE,
    T_SCORE,
    T_TAP,
    T_BACK,
    T_HINT,
    T_SETTINGS,
    T_MENU,
    T_PAUSED,
    T_RESUME,
    T_GAMEOVER,
    T_RETRY,
    T_LOSE,
    T_RESET,
    T_CHOOSE,
    T_HEIR_TITLE,
    T_MUSIC,
    T_SFX,
    T_RESET_HINT,
    T_COUNT,
} i18n_key_t;

void i18n_set(i18n_lang_t lang);
i18n_lang_t i18n_get(void);
void i18n_cycle(void); /* EN -> RU -> TR -> EN */

const char *i18n(i18n_key_t key);              /* current-language string for key */
const char *i18n_lang_label(i18n_lang_t lang); /* endonym, e.g. "Русский" */

/* Inline 3-language pick (EN/RU/TR) for one-off strings outside the key table. Args must
   outlive the frame (string literals do). Usable from any module that includes i18n.h. */
const char *pick_lang(const char *en, const char *ru, const char *tr);

#endif /* TJ_I18N_H */
