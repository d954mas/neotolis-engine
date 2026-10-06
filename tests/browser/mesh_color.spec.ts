import { test, expect } from '@playwright/test';

type MeshHooks = {
  ready: boolean;
  programs_ready(): boolean;
  mesh_color_probe?: () => number;
};

test('mesh and skinned instances carry their RGBA8 color in WebGL2', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/');
  await page.waitForFunction(() => {
    const hooks = (window as unknown as { __nt?: MeshHooks }).__nt;
    return hooks?.ready && hooks.programs_ready();
  });

  /* Bits: tinted and white mesh instances of one run, a shader without a_color,
   * tinted and white skinned instances of one run. */
  await expect
    .poll(() =>
      page.evaluate(() => {
        const probe = (window as unknown as { __nt?: MeshHooks }).__nt?.mesh_color_probe;
        return probe ? probe() >>> 0 : -1;
      }),
    )
    .toBe(0x1f);
  expect(errors).toEqual([]);
});
