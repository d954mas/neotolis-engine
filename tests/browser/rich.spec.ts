import { test, expect } from '@playwright/test';

// Window hooks the ui_showcase web build exposes (examples/ui_showcase/main.c, gated behind
// NT_SHOWCASE_TEST_HOOKS). The rich-text surface mirrors the input surface: switch to the Rich tab,
// read the block's on-canvas CSS rect, the per-frame walker text-command count, the game-owned
// <link> click counter, and the quest link's EXACT on-canvas CSS rect (so the click is precise).
declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      walk_text_cmd_count(): number;
      open_rich_tab(): void;
      rich_visible(): boolean;
      rich_css(): { x: number; y: number; w: number; h: number };
      rich_link_css(): { present: boolean; x: number; y: number; w: number; h: number };
      rich_link_clicks(): number;
    };
  }
}

// SECOND headless-browser smoke test: a real Chromium proves the rich-text self-emit reaches the actual
// web render path (the inline glyphs/spans route through nt_text_renderer_draw_n during emit_custom),
// and that a precise click on the inline <link> registers a game-owned click (link interaction path).
test('rich text: self-emit reaches the web render path (emit count > 0) and the link click registers', async ({ page }) => {
  await page.goto('/index.html');

  // Boot gate: the wasm app sets __nt.ready after its first rendered frame.
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });

  // Switch to the Rich Text tab, then wait until the code-first rich block reports a measured bbox
  // (laid out + self-emitting this frame).
  await page.evaluate(() => window.__nt!.open_rich_tab());
  await page.waitForFunction(() => window.__nt!.rich_visible() === true, null, { timeout: 10_000 });

  // PRIMARY assertion: the rich tab emitted text commands end-to-end in real Chromium. The rich blocks
  // dominate this tab's text, so a positive count proves the solved spans reached the walker's text path
  // (a silent emit failure -- e.g. the CUSTOM dispatch not rebinding the text material -- would read 0).
  await expect
    .poll(() => page.evaluate(() => window.__nt!.walk_text_cmd_count()), { timeout: 10_000 })
    .toBeGreaterThan(0);

  // SECONDARY (HARD gate): click the inline quest <link>. The widget exposes the link's EXACT on-canvas
  // CSS rect (res_a.first_link_rect mapped through the block bbox), so we click its center precisely
  // instead of sweeping a band. Wait until the link rect is resolved (it needs the block's prev-frame
  // bbox, so it lands a frame after rich_visible).
  await page.waitForFunction(() => window.__nt!.rich_link_css().present === true, null, { timeout: 10_000 });

  const before = await page.evaluate(() => window.__nt!.rich_link_clicks());
  // ONE precise pointerdown->up at the link center. A tiny bounded retry (<=3) on the SAME center absorbs
  // a rare cold-frame miss (the click latch needs press+release on the same link); the expect is HARD.
  for (let attempt = 0; attempt < 3; attempt++) {
    const link = await page.evaluate(() => window.__nt!.rich_link_css());
    await page.mouse.move(link.x, link.y);
    await page.waitForTimeout(16);
    await page.mouse.down();
    await page.waitForTimeout(40);
    await page.mouse.up();
    await page.waitForTimeout(60);
    if ((await page.evaluate(() => window.__nt!.rich_link_clicks())) > before) break;
  }

  // HARD assertion: the precise link click bumped the game-owned counter exactly once-or-more.
  expect(await page.evaluate(() => window.__nt!.rich_link_clicks())).toBe(before + 1);
});
