import { test, expect } from '@playwright/test';

type ShapeHooks = {
  ready: boolean;
  programs_ready(): boolean;
  shape_stroke_probe?: () => number;
};

test('shape strokes render joined and pixel-width geometry in WebGL2', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  await page.waitForFunction(() => {
    const hooks = (window as unknown as { __nt?: ShapeHooks }).__nt;
    return hooks?.ready && hooks.programs_ready();
  });

  await expect
    .poll(() =>
      page.evaluate(() => {
        const probe = (window as unknown as { __nt?: ShapeHooks }).__nt?.shape_stroke_probe;
        return probe ? probe() >>> 0 : -1;
      }),
    )
    .toBe(0xff);
  expect(errors).toEqual([]);
});
