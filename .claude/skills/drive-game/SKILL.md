---
name: drive-game
description: Drive the Turkic Jam game live over the nt_devapi TCP bus on PC (native) — inspect the UI tree, read entities, emulate input, and run a bot that plays. Use when testing or iterating the game, verifying a change actually works in the running app, clicking buttons / pressing keys programmatically, dumping the live UI, or scripting a bot. Triggers: test the game, drive/play the game, click the button, read the UI tree, emulate input, bot, devapi.
---

# Drive the game via nt_devapi (PC / native)

`nt_devapi` is the engine debug command-bus. The running game opens a TCP server on
`127.0.0.1`; you send one text command line and get one JSON line back. Use it to
inspect UI/entities and inject input — i.e. to test, iterate, and run bots.

Browser/WASM uses exported functions + Playwright instead (not covered here).

## 1. Build (native-debug enables NT_DEVAPI_ENABLED)

```
cmake --build build/_cmake/native-debug --target turkic_jam
```
First time / after pack changes also run the packs task:
`cmake --build build/_cmake/native-debug --target build_turkic_jam_packs && build\games\turkic-jam-2026\native-debug\build_turkic_jam_packs.exe build/games/turkic-jam-2026`

## 2. Launch with the server (cwd MUST be the exe dir so it finds assets/)

PowerShell, backgrounded so it doesn't block:
```powershell
$exe = Resolve-Path "build\games\turkic-jam-2026\native-debug\turkic_jam.exe"
$wd  = Resolve-Path "build\games\turkic-jam-2026\native-debug"
$p = Start-Process -FilePath $exe -WorkingDirectory $wd -ArgumentList "--devapi","9123" -PassThru
Start-Sleep -Seconds 4   # window + async asset load
# ... drive it (step 3) ...
Stop-Process -Id $p.Id -Force
```

## 3. Connect & send commands

Use the Python client (one connection, reads stdin lines, prints JSON):
```
python tools\devapi\devapi_cli.py 9123 ui.tree           # one command
python tools\devapi\devapi_bot_demo.py 9123              # full observe->act->observe demo
```
Do NOT pipe a PowerShell string into the CLI (`"..." | python ...`) — PS may prepend a
UTF-8 BOM to the first line, breaking the first command. Pipe via a file or a Python script.

## 4. Protocol

Request = `endpoint key=value ...` (console-style). Response = one JSON line
`{"ok":true,"data":...}` or `{"ok":false,"error":"..."}`.

| Endpoint | Args | Returns |
|----------|------|---------|
| `ping` | — | `{"ok":true}` |
| `endpoints` | — | list of endpoint names |
| `view` | — | `{fb_w,fb_h,logical_w,logical_h}` |
| `ui.tree` | — | array of `{id,depth,name,text?,x,y,w,h}` (logical coords, Y-down) |
| `ui.element` | `id=<clayId>` | bbox + bg + font of one element |
| `entity.list` | — | array of `{entity,x,y,sprite_region?,color?,visible?}` |
| `input.key` | `key=<K> mode=<tap\|down\|up>` | `{"ok":true}` |
| `input.move` | `x=<f> y=<f>` | `{"ok":true}` |
| `input.click` | `x=<f> y=<f> button=<left\|right\|middle>` | `{"ok":true}` |
| `input.click_ui` | `id=<clayId>` | clicks element centre (logical→fb) |
| `input.button` | `button=<...> state=<down\|up>` | held button |

`key=` accepts single letters/digits (`P`, `5`) and names (`SPACE ENTER ESCAPE TAB UP DOWN LEFT RIGHT ...`).

## 5. Clicking a button (the reliable way)

`input.click` takes framebuffer pixels; `ui.tree` gives logical bbox. In EXPAND mode there is
no letterbox, so `fb = logical * fb_size/logical_size` (often 1:1 at 1280×720). The robust
recipe: read `ui.tree`, find the row whose `text` is the label, click its bbox centre:
```
center = (x + w/2, y + h/2)   # from the ui.tree row
input.click x=<cx> y=<cy>
```
A click resolves over ~2 frames (press then release) — wait ~0.3 s before re-reading the tree.

## 6. Bot loop pattern

```
loop:
  obs   = req("ui.tree")            # observe
  target = find row by text/name    # decide
  req(f"input.click x={cx} y={cy}") # act
  sleep(0.3); verify via ui.tree    # observe again
```
`tools/devapi/devapi_bot_demo.py` is a working example (clicks START, confirms the scene swap).

## 7. Notes
- Only one client at a time (a new connection replaces the old).
- `ui.tree` reflects the previous frame's layout (1-frame lag) — fine for bots.
- Everything is a no-op unless built with `NT_DEVAPI_ENABLED` (native-debug preset).
- Always `Stop-Process` the game when done.
