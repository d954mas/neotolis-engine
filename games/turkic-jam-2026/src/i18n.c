#include "i18n.h"

/* Source is UTF-8; clang treats it as UTF-8 by default. The packed font must
 * include these glyphs (see TJ_CHARSET in build_packs.c). */
static const char *s_table[LANG_COUNT][T_COUNT] = {
    [LANG_EN] =
        {
            [T_TITLE] = "SONG OF TAMGA",
            [T_SUBTITLE] = "A steppe loop-builder about memory, road and clan",
            [T_START] = "START",
            [T_LANGUAGE] = "Language",
            [T_GAME_TITLE] = "On the Road",
            [T_SCORE] = "Circle",
            [T_TAP] = "Walk",
            [T_BACK] = "BACK",
            [T_HINT] = "Choose a path. Carry the clan memory forward.",
            [T_SETTINGS] = "Settings",
            [T_MENU] = "Menu",
            [T_PAUSED] = "Paused",
            [T_RESUME] = "Resume",
            [T_GAMEOVER] = "Game Over",
            [T_RETRY] = "Retry",
            [T_LOSE] = "End Run",
            [T_RESET] = "Reset progress",
            [T_CHOOSE] = "Choose",
            [T_HEIR_TITLE] = "Choose an Heir",
        },
    [LANG_RU] =
        {
            [T_TITLE] = "ПЕСНЬ ТАМГИ",
            [T_SUBTITLE] = "Степной loop-builder о памяти, дороге и роде",
            [T_START] = "НАЧАТЬ",
            [T_LANGUAGE] = "Язык",
            [T_GAME_TITLE] = "В пути",
            [T_SCORE] = "Круг",
            [T_TAP] = "Идти",
            [T_BACK] = "НАЗАД",
            [T_HINT] = "Выбери путь. Пронеси память рода дальше.",
            [T_SETTINGS] = "Настройки",
            [T_MENU] = "Меню",
            [T_PAUSED] = "Пауза",
            [T_RESUME] = "Продолжить",
            [T_GAMEOVER] = "Игра окончена",
            [T_RETRY] = "Заново",
            [T_LOSE] = "Завершить путь",
            [T_RESET] = "Сбросить прогресс",
            [T_CHOOSE] = "Выбрать",
            [T_HEIR_TITLE] = "Выбери наследника",
        },
    [LANG_TR] =
        {
            [T_TITLE] = "TAMGA EZGISI",
            [T_SUBTITLE] = "Bellek, yol ve oba hakkında bozkır loop-builder",
            [T_START] = "BAŞLA",
            [T_LANGUAGE] = "Dil",
            [T_GAME_TITLE] = "Yolda",
            [T_SCORE] = "Döngü",
            [T_TAP] = "Yürü",
            [T_BACK] = "GERİ",
            [T_HINT] = "Yolu seç. Obanın belleğini ileri taşı.",
            [T_SETTINGS] = "Ayarlar",
            [T_MENU] = "Menü",
            [T_PAUSED] = "Duraklatıldı",
            [T_RESUME] = "Devam",
            [T_GAMEOVER] = "Oyun Bitti",
            [T_RETRY] = "Tekrar",
            [T_LOSE] = "Yolu Bitir",
            [T_RESET] = "İlerlemeyi sıfırla",
            [T_CHOOSE] = "Seç",
            [T_HEIR_TITLE] = "Varis Seç",
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
