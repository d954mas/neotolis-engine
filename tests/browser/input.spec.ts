import { test, expect } from '@playwright/test';

type InputSnapshot = {
  keys: number[][]; // [down, pressed, released], indexed by nt_key_t
  pointers: { id: number; active: number; down: number; pressed: number; released: number; x: number; y: number; wheel: number }[];
  chars: number[];
};

// Window hooks the browser_smoke web build exposes (tests/browser/app/main.c, #ifdef __EMSCRIPTEN__).
declare global {
  interface Window {
    __blurTouch?: InputSnapshot[];
    __nt?: {
      ready: boolean;
      input_buffer(): string;
      input_poll(): InputSnapshot;
      walk_text_cmd_count(): number;
      field_visible(): boolean;
      field_css(): { x: number; y: number };
      mouse(): { active: boolean; down: boolean };
    };
  }
}

// FIRST headless-browser smoke test: a real Chromium drives the genuine web input + clipboard path
// (keydown incl. Cyrillic -> _ntCharBuf ring; DOM paste -> nt_clipboard cache) against the
// browser_smoke test app. No DOM stubbing -- the layer under test is the real engine glue.
test('input field: real keydown (Cyrillic) + paste reaches the buffer and the render path', async ({ page }) => {
  await page.goto('/index.html');

  // Frame barrier: wait for a real rendered frame. Every input event below is frame-synchronized through
  // this so the engine drains it before the next -- robust at any FPS. Fixed-ms settles bunch on a slow
  // (CI swiftshader) frame and drop modifier+key combos / leave a select-all unestablished, which is the
  // CI-only failure this replaces (the typed text replaced only part of the prefill).
  const frame = () => page.evaluate(() => new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r()))));

  // Boot gate: the wasm app sets __nt.ready after its first rendered frame.
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });

  // The field is always laid out (no tabs in browser_smoke); wait until it reports a measured bbox.
  await page.waitForFunction(() => window.__nt!.field_visible() === true, null, { timeout: 10_000 });

  // Focus the REAL widget by clicking its canvas CSS coordinates -> genuine pointerdown -> field focus.
  // Two passes give headroom against a rare cold-frame miss (the typing assert below is the real gate).
  const { x, y } = await page.evaluate(() => window.__nt!.field_css());
  for (let i = 0; i < 2; i++) {
    await page.mouse.move(x, y);
    await frame();
    await page.mouse.down();
    await frame();
    await page.mouse.up();
    await frame();
  }

  // Select-all so the typed text replaces the prefill ("Привет, мир"). Frame-sync each key so the engine
  // drains Ctrl-down (ctrl_held) BEFORE 'a' (NT_KEY_A pressed -> select-all); a fixed settle here races
  // the slow CI frame, leaving the selection unestablished so the type replaces only part of the prefill.
  await page.keyboard.down('Control');
  await frame();
  await page.keyboard.down('a');
  await frame();
  await page.keyboard.up('a');
  await frame();
  await page.keyboard.up('Control');
  await frame();

  // Type 'Привет' via REAL keydown events on the canvas. Playwright's keyboard.type() maps chars to the
  // US physical layout and sends non-Latin (Cyrillic) via insertText -- which fires NO keydown, so the
  // engine's keydown->_ntCharBuf path never sees it. Dispatching genuine KeyboardEvent('keydown') with
  // the Cyrillic e.key drives the exact same listener a Cyrillic keyboard would (the path under test);
  // only the event ORIGIN is synthetic, unavoidable for non-US chars. The first char replaces the
  // select-all selection; frame-sync each so the char ring drains it.
  for (const ch of 'Привет') {
    await page.evaluate((k) => document.querySelector('canvas')!.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true })), ch);
    await frame();
  }
  // Confirm the whole edit landed (catches a raced select-all/type deterministically) before pasting.
  await expect.poll(() => page.evaluate(() => window.__nt!.input_buffer()), { timeout: 10_000 }).toBe('Привет');

  // Real clipboard paste path: write 'X' to the OS clipboard, then Ctrl+V. The genuine browser paste
  // event (fired by Ctrl+V) carries the OS clipboard text into the engine's `document` paste listener ->
  // nt_clipboard cache; the field's Ctrl+V handler then reads the cache and inserts. (A synthetic
  // ClipboardEvent does NOT work: Ctrl+V triggers Chromium's own paste event which overwrites the cache
  // with the real OS clipboard.) Frame-synced + sent ONCE, so the paste can't double-insert.
  await page.evaluate(() => navigator.clipboard.writeText('X'));
  await frame();
  await page.keyboard.down('Control');
  await frame();
  await page.keyboard.down('v');
  await frame();
  await page.keyboard.up('v');
  await frame();
  await page.keyboard.up('Control');

  // Assert the genuine game-owned buffer (exact match -- a silent drop or reorder fails).
  await expect
    .poll(() => page.evaluate(() => window.__nt!.input_buffer()), { timeout: 10_000 })
    .toBe('ПриветX');

  // The typed text must also reach the render path (walker emitted text commands this frame).
  const cmdCount = await page.evaluate(() => window.__nt!.walk_text_cmd_count());
  expect(cmdCount).toBeGreaterThan(0);
});

// A real mouse dragged from the canvas and released outside it: the captured release lands and the mouse
// leaves (not every browser sends a pointerleave after a captured drag), so no button sticks.
test('mouse: a drag released outside the canvas releases and leaves', async ({ page }) => {
  await page.goto('/index.html');
  const frame = () => page.evaluate(() => new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r()))));
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  const box = await page.evaluate(() => {
    const c = document.querySelector('canvas')!;
    c.style.width = '50vw';
    c.style.height = '50vh';
    return { w: window.innerWidth, h: window.innerHeight };
  });
  await frame();
  await frame();
  await page.mouse.move(box.w * 0.25, box.h * 0.25);
  await frame();
  await page.mouse.down();
  await frame();
  expect(await page.evaluate(() => window.__nt!.mouse())).toEqual({ active: true, down: true });
  await page.mouse.move(box.w * 0.9, box.h * 0.9, { steps: 4 });
  await frame();
  expect(await page.evaluate(() => window.__nt!.mouse())).toEqual({ active: true, down: true });
  await page.mouse.up();
  await frame();
  await frame();
  expect(await page.evaluate(() => window.__nt!.mouse())).toEqual({ active: false, down: false });
});

// A blur during a drag releases the mouse; the canvas still holds the capture, so the next move must not
// press again (the buttons belong to the canvas only from a press on it).
test('mouse: a blur during a drag releases it for good', async ({ page }) => {
  await page.goto('/index.html');
  const frame = () => page.evaluate(() => new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r()))));
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  const vp = page.viewportSize()!;
  await page.mouse.move(vp.width * 0.5, vp.height * 0.5);
  await frame();
  await page.mouse.down();
  await frame();
  expect(await page.evaluate(() => window.__nt!.mouse())).toEqual({ active: true, down: true });
  await page.evaluate(() => {
    const i = document.createElement('input');
    document.body.appendChild(i);
    i.focus();
  });
  await frame();
  await page.mouse.move(vp.width * 0.5 + 10, vp.height * 0.5, { steps: 3 });
  await frame();
  expect(await page.evaluate(() => window.__nt!.mouse())).toEqual({ active: true, down: false });
  await page.mouse.up();
});

test('blur: the next poll only releases; keys and UTF-32 chars after refocus follow', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  // One JS task prevents the frame loop from polling between blur and the new input.
  const { held, current, next } = await page.evaluate(() => {
    const canvas = document.querySelector('canvas')!;
    const key = (code: string, value: string) => canvas.dispatchEvent(new KeyboardEvent('keydown', { code, key: value, bubbles: true }));
    canvas.focus();
    key('KeyH', 'h');
    const held = window.__nt!.input_poll();
    key('KeyB', 'b');
    canvas.dispatchEvent(new WheelEvent('wheel', { deltaY: 120, clientX: 100, clientY: 100 }));
    canvas.blur();
    canvas.focus();
    key('KeyA', '😀');
    canvas.dispatchEvent(new WheelEvent('wheel', { deltaY: 240, clientX: 100, clientY: 100 }));
    const current = window.__nt!.input_poll();
    const next = window.__nt!.input_poll();
    return { held, current, next };
  });
  expect(held.keys[7]).toEqual([1, 1, 0]); // H was already held when focus was lost.
  expect(held.chars).toEqual(['h'.codePointAt(0)]);
  expect(current.keys[7]).toEqual([0, 0, 1]);
  expect(current.keys[0]).toEqual([0, 0, 0]);
  expect(current.chars).toEqual([]);
  expect(next.keys[0]).toEqual([1, 1, 0]);
  expect(next.keys[1]).toEqual([0, 0, 0]); // B and its char were queued before blur.
  expect(next.keys[7]).toEqual([0, 0, 0]);
  expect(next.chars).toEqual([0x1f600]);
  expect(next.pointers[0].wheel).toBe(2);
  expect(errors).toEqual([]);
});

test('blur: a new touch before poll takes the released slot and follows later moves', async ({ page, context }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  const frame = () => page.evaluate(() => new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve()))));
  const cdp = await context.newCDPSession(page);
  const oldTouch = { id: 11, x: 120, y: 240, force: 0.5 };
  const newTouch = { id: 22, x: 350, y: 300, force: 0.5 };
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [oldTouch] });
  await frame();
  const held = await page.evaluate(() => window.__nt!.input_poll().pointers[1]);
  expect(held).toMatchObject({ active: 1, down: 1, x: 120, y: 240 });
  await page.evaluate((oldId) => {
    const canvas = document.querySelector('canvas')!;
    // Capture runs before the engine's genuine pointerdown listener; the final listener polls after it.
    canvas.addEventListener('pointerdown', () => {
      canvas.dispatchEvent(new PointerEvent('pointermove', { pointerType: 'touch', pointerId: oldId, clientX: 333, clientY: 444, buttons: 1 }));
      canvas.blur();
      canvas.focus();
    }, { capture: true, once: true });
    canvas.addEventListener('pointerdown', () => {
      window.__blurTouch = [window.__nt!.input_poll(), window.__nt!.input_poll()];
    }, { once: true });
  }, held.id);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [oldTouch, newTouch] });
  const [current, next] = await page.evaluate(() => window.__blurTouch!);
  expect(current.pointers[1]).toMatchObject({ id: held.id, active: 1, down: 0, released: 1, x: 120, y: 240 });
  expect(current.pointers[2].active).toBe(0);
  expect(next.pointers[1]).toMatchObject({ active: 1, down: 1, pressed: 1, released: 0, x: 350, y: 300 });
  const newId = next.pointers[1].id;
  expect(newId).not.toBe(held.id);
  await cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: [{ ...oldTouch, x: 130 }, { ...newTouch, x: 390, y: 320 }] });
  await frame();
  const moved = await page.evaluate(() => window.__nt!.input_poll());
  expect(moved.pointers.slice(1).filter((p) => p.active)).toMatchObject([{ id: newId, down: 1, x: 390, y: 320 }]);
  expect(errors).toEqual([]);
});
