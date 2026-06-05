# Конфиги баланса «Песнь Тамги»

**Граница ответственности:** эти файлы = баланс игры. Их правит GDD-агент.
Кор (C-код в `src/`) читает их в рантайме и ничего не хардкодит из баланса.
Менять числа здесь можно без перекомпиляции C (нужен только перезапуск, либо
`--config <dir>` чтобы указать на этот каталог напрямую).

> Отклонение от спеки движка («no runtime parsers»): осознанное, ради скорости
> итераций на джеме. Для релиза конфиги можно запаковать в `.ntpack`.

## Файлы

| Файл | Что | Формат |
|------|-----|--------|
| `balance.ini` | скаляры (длина пути, круги, формулы, проценты) | `key = value`, `#` коммент |
| `heirs.tsv` | наследники (архетипы) | `|`-таблица, `#` коммент |
| `tiles.tsv` | тайлы пустыни | `|`-таблица, `#` коммент |

`.tsv` — поля разделены `|`, пробелы по краям обрезаются, строки на `#` и пустые
игнорируются. Первая нешапочная строка = данные (шапку пиши как `#`-комментарий).

## Контракт (что кор ожидает)

**balance.ini** — ключи (все целые, кроме `move_seconds_per_cell` — float):
`start_in_game, path_cells, laps_to_win, start_stamina, move_seconds_per_cell,
check_base_difficulty, check_difficulty_per_circle, check_fail_stamina_loss,
check_fail_reward_pct, tamga_wisdom_base, tamga_wisdom_per_circle,
tamga_wisdom_slot_div, tamga_glory_div, tamga_max_active,
death_keep_supplies_pct, death_keep_wisdom_pct, death_keep_glory_pct`.
Незаданные ключи берут значение по умолчанию из кода (`config.c`).

**heirs.tsv** — колонки: `id | name | body | mind | spirit | stamina_bonus`.

**tiles.tsv** — колонки:
`id | name | kind | check | diff_base | diff_per_circle | supplies | wisdom | glory | stamina_cost | stamina_restore`
- `kind`: `safe` | `support` | `check`
- `check`: `none` | `body` | `mind` | `spirit`

## Расширение

Добавить тайл/наследника = добавить строку (id уникален). Добавить балансный
скаляр = ключ в `balance.ini` + поле в `game_config_t` (`config.c`). Синергии,
испытания круга, апгрейды аула — отдельные таблицы, появятся по мере роста кора
(`synergies.tsv`, `trials.tsv`, `aul.tsv`).
