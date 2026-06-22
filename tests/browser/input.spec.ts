import { test, expect } from '@playwright/test';

// Window hooks the ui_showcase web build exposes (examples/ui_showcase/main.c, #ifdef __EMSCRIPTEN__).
declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      input_buffer(): string;
      walk_text_cmd_count(): number;
      open_input_tab(): void;
      field_visible(): boolean;
      field_css(): { x: number; y: number };
    };
  }
}

// FIRST headless-browser smoke test: a real Chromium drives the genuine web input + clipboard path
// (keydown incl. Cyrillic -> _ntCharBuf ring; DOM paste -> nt_clipboard cache) against the actual
// ui_showcase build. No DOM stubbing -- the layer under test is the real engine glue.
test('input field: real keydown (Cyrillic) + paste reaches the buffer and the render path', async ({ page }) => {
  await page.goto('/index.html');

  // Boot gate: the wasm app sets __nt.ready after its first rendered frame.
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });

  // Switch to the Input tab so the field is laid out, then wait until it reports a measured bbox.
  await page.evaluate(() => window.__nt!.open_input_tab());
  await page.waitForFunction(() => window.__nt!.field_visible() === true, null, { timeout: 10_000 });

  // Focus the REAL widget by clicking its canvas CSS coordinates -> genuine pointerdown -> field focus.
  // A single fast mouse.click() can land its pointerdown on a frame where the field is not yet
  // hit-testable; a deliberate move + down + up with per-frame settles drives the genuine focus path.
  // Two passes give headroom against a rare cold-frame miss (the typing assert below is the real gate).
  const { x, y } = await page.evaluate(() => window.__nt!.field_css());
  for (let i = 0; i < 2; i++) {
    await page.mouse.move(x, y);
    await page.waitForTimeout(20);
    await page.mouse.down();
    await page.waitForTimeout(50);
    await page.mouse.up();
    await page.waitForTimeout(70);
  }

  // The Cyrillic field starts pre-filled ("Привет, мир"); select-all so the typed text replaces it.
  // Hold Control across its own poll frame: a fused press('Control+a') can drain the keyup before the
  // engine sees 'A' held (synthetic events are drained per frame), so down/up explicitly with settles.
  await page.keyboard.down('Control');
  await page.waitForTimeout(40);
  await page.keyboard.press('a');
  await page.waitForTimeout(40);
  await page.keyboard.up('Control');
  await page.waitForTimeout(40);

  // Type 'Привет' via REAL keydown events on the canvas. Playwright's keyboard.type() maps chars to the
  // US physical layout and sends non-Latin (Cyrillic) via insertText -- which fires NO keydown, so the
  // engine's keydown->_ntCharBuf path never sees it. Dispatching genuine KeyboardEvent('keydown') with
  // the Cyrillic e.key drives the exact same listener a Cyrillic keyboard would (the path under test);
  // only the event ORIGIN is synthetic, unavoidable for non-US chars in any headless harness. One char
  // per poll-frame interval so the ring drains each; the first replaces the Ctrl+A selection.
  await page.evaluate(async (text) => {
    const canvas = document.querySelector('canvas')!;
    const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
    for (const ch of text) {
      canvas.dispatchEvent(new KeyboardEvent('keydown', { key: ch, bubbles: true, cancelable: true }));
      await sleep(24);
    }
  }, 'Привет');
  await page.waitForTimeout(60);

  // Real clipboard paste path: write 'X' to the OS clipboard, then Ctrl+V. The genuine browser paste
  // event (fired by Ctrl+V on the canvas) carries the OS clipboard text into the engine's `document`
  // paste listener -> nt_clipboard cache; the field's Ctrl+V handler then reads the cache and inserts.
  // (A synthetic ClipboardEvent does NOT work here: Ctrl+V triggers Chromium's own paste event which
  // overwrites the cache with the real OS clipboard, so the OS clipboard must hold the text.)
  await page.evaluate(() => navigator.clipboard.writeText('X'));
  await page.waitForTimeout(60);
  await page.keyboard.down('Control');
  await page.waitForTimeout(50);
  await page.keyboard.down('v');
  await page.waitForTimeout(50);
  await page.keyboard.up('v');
  await page.waitForTimeout(50);
  await page.keyboard.up('Control');

  // Assert the genuine game-owned buffer (exact match -- a silent drop or reorder fails).
  await expect
    .poll(() => page.evaluate(() => window.__nt!.input_buffer()), { timeout: 10_000 })
    .toBe('ПриветX');

  // The typed text must also reach the render path (walker emitted text commands this frame).
  const cmdCount = await page.evaluate(() => window.__nt!.walk_text_cmd_count());
  expect(cmdCount).toBeGreaterThan(0);
});
