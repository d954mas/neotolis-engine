import { test, expect } from '@playwright/test';

// Hooks the browser_smoke web build exposes (tests/browser/app/main.c).
declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      drawn_frames(): number;
      programs_ready(): boolean;
    };
  }
}

// The only test that drives a REAL WebGL context loss. Everything else about the
// recovery contract -- programs die and are relinked under new handles, materials
// keep their handles, renderers drop their caches, the game's per-frame gate
// brings it all back -- is otherwise verified against the stub backend only, so
// this is the one place the browser can contradict us.
test('context loss: rendering stops, then the game relinks and resumes', async ({ page }) => {
  await page.goto('/index.html');

  const frame = () =>
    page.evaluate(() => new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r()))));

  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  await page.waitForFunction(() => window.__nt!.programs_ready() === true, null, { timeout: 10_000 });

  // Baseline: the app is drawing.
  const before = await page.evaluate(() => window.__nt!.drawn_frames());
  await frame();
  await frame();
  expect(await page.evaluate(() => window.__nt!.drawn_frames())).toBeGreaterThan(before);

  // Kill the context the way a driver reset does.
  const lost = await page.evaluate(() => {
    const canvas = document.querySelector('canvas') as HTMLCanvasElement | null;
    const gl = canvas?.getContext('webgl2') as WebGL2RenderingContext | null;
    const ext = gl?.getExtension('WEBGL_lose_context');
    if (!ext) return false;
    ext.loseContext();
    // The browser only delivers webglcontextrestored after a restoreContext call.
    setTimeout(() => ext.restoreContext(), 100);
    return true;
  });
  expect(lost, 'WEBGL_lose_context unavailable').toBe(true);

  // While the context is gone and the stages have not re-activated, the programs
  // are not ready -- the material still names a handle whose GPU object is dead.
  await page.waitForFunction(() => window.__nt!.programs_ready() === false, null, { timeout: 10_000 });

  // And drawing actually stopped -- readiness going false proves nothing on its
  // own. Phrased as "no draws while no program is ready" rather than a flat
  // equality, so the relink completing mid-sample is not a flake.
  const stalled = await page.evaluate(() => window.__nt!.drawn_frames());
  await frame();
  await frame();
  const resumed = await page.evaluate(() => ({ n: window.__nt!.drawn_frames(), ready: window.__nt!.programs_ready() }));
  expect(resumed.n === stalled || resumed.ready, 'frames were drawn while no program was ready').toBeTruthy();

  // Recovery is the game's own build gate running again: it relinks from the
  // re-activated stages and re-assigns. Nothing in the engine does this by itself.
  await page.waitForFunction(() => window.__nt!.programs_ready() === true, null, { timeout: 30_000 });

  // And drawing actually resumes -- readiness alone would not prove the renderers
  // rebuilt their pipeline caches on the new program.
  const afterRelink = await page.evaluate(() => window.__nt!.drawn_frames());
  // The atlas and fonts re-activate on their own schedule after the invalidate,
  // so wait for the frame counter rather than assuming a fixed number of frames.
  await page.waitForFunction((n) => window.__nt!.drawn_frames() > n, afterRelink, { timeout: 30_000 });

  // No assert fired on the way through: a trapped wasm module stops answering.
  expect(await page.evaluate(() => window.__nt?.ready === true)).toBe(true);
});
