#include "i18n.h"

/* Source is UTF-8; clang treats it as UTF-8 by default. The packed font must
 * include these glyphs (see TJ_CHARSET in build_packs.c). */
static const char *s_table[LANG_COUNT][T_COUNT] = {
    [LANG_EN] =
        {
            [T_TITLE] = "TURKIC JAM 2026",
            [T_SUBTITLE] = "Engine base template",
            [T_START] = "START",
            [T_LANGUAGE] = "Language",
            [T_GAME_TITLE] = "Playing",
            [T_SCORE] = "Score",
            [T_TAP] = "TAP +1",
            [T_BACK] = "BACK",
            [T_HINT] = "L = language    Esc = quit",
            [T_SETTINGS] = "Settings",
            [T_MENU] = "Menu",
            [T_PAUSED] = "Paused",
            [T_RESUME] = "Resume",
            [T_GAMEOVER] = "Game Over",
            [T_RETRY] = "Retry",
            [T_LOSE] = "Lose",
            [T_RESET] = "Reset progress",
        },
    [LANG_RU] =
        {
            [T_TITLE] = "ТЮРКСКИЙ ДЖЕМ 2026",
            [T_SUBTITLE] = "Базовый шаблон движка",
            [T_START] = "НАЧАТЬ",
            [T_LANGUAGE] = "Язык",
            [T_GAME_TITLE] = "Игра",
            [T_SCORE] = "Счёт",
            [T_TAP] = "ТАП +1",
            [T_BACK] = "НАЗАД",
            [T_HINT] = "L = язык    Esc = выход",
            [T_SETTINGS] = "Настройки",
            [T_MENU] = "Меню",
            [T_PAUSED] = "Пауза",
            [T_RESUME] = "Продолжить",
            [T_GAMEOVER] = "Игра окончена",
            [T_RETRY] = "Заново",
            [T_LOSE] = "Проиграть",
            [T_RESET] = "Сбросить прогресс",
        },
    [LANG_TR] =
        {
            [T_TITLE] = "TÜRK JAM 2026",
            [T_SUBTITLE] = "Motor temel şablonu",
            [T_START] = "BAŞLA",
            [T_LANGUAGE] = "Dil",
            [T_GAME_TITLE] = "Oyun",
            [T_SCORE] = "Skor",
            [T_TAP] = "DOKUN +1",
            [T_BACK] = "GERİ",
            [T_HINT] = "L = dil    Esc = çıkış",
            [T_SETTINGS] = "Ayarlar",
            [T_MENU] = "Menü",
            [T_PAUSED] = "Duraklatıldı",
            [T_RESUME] = "Devam",
            [T_GAMEOVER] = "Oyun Bitti",
            [T_RETRY] = "Tekrar",
            [T_LOSE] = "Kaybet",
            [T_RESET] = "İlerlemeyi sıfırla",
        },
};

static const char *s_lang_label[LANG_COUNT] = {
    [LANG_EN] = "English",
    [LANG_RU] = "Русский",
    [LANG_TR] = "Türkçe",
};

static i18n_lang_t s_lang = LANG_EN;

void i18n_set(i18n_lang_t lang) {
    if (lang >= 0 && lang < LANG_COUNT) {
        s_lang = lang;
    }
}

i18n_lang_t i18n_get(void) { return s_lang; }

void i18n_cycle(void) { s_lang = (i18n_lang_t)((s_lang + 1) % LANG_COUNT); }

const char *i18n(i18n_key_t key) {
    if (key < 0 || key >= T_COUNT) {
        return "?";
    }
    const char *s = s_table[s_lang][key];
    return s ? s : s_table[LANG_EN][key];
}

const char *i18n_lang_label(i18n_lang_t lang) {
    if (lang < 0 || lang >= LANG_COUNT) {
        return "?";
    }
    return s_lang_label[lang];
}
