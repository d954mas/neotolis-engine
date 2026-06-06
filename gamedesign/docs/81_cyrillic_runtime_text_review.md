# 81. Cyrillic runtime text review

Status: Code fix accepted by GDD.

Reviewed file:

```text
games/turkic-jam-2026/src/view.c
```

Reviewed screenshot:

```text
tmp/cyrillic_ui_check.png
```

Decision:

```text
ACCEPTED FOR CURRENT PLAYABLE
```

What Code found:

```text
UTF-8 rendering path works
config/log/HUD/hero stats render readable Russian text
confirmed broken runtime literal was the empty card label in view.c
previous hero title mojibake came from a temporary QA config created with bad PowerShell text conversion
```

Fix:

```text
"РїСѓСЃС‚Рѕ" -> "пусто"
```

Screenshot evidence:

```text
Охотник
Тело
Ум
Дух
Силы
Клетка
пусто
```

Verification from Code:

```text
clang-format --dry-run --Werror games/turkic-jam-2026/src/view.c -> PASS
cmake --build build/_cmake/native-debug --target turkic_jam -> PASS
desktop dump tmp/cyrillic_ui_check.png -> PASS/nonblank
```

GDD note:

This removes the current runtime Cyrillic readability blocker. Future QA configs must be created with UTF-8-safe tooling; PowerShell text conversion can create false mojibake screenshots.

