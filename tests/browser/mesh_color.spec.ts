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

  /* 0x80000000: the probe's programs are still linking. Otherwise one bit per region:
   * tinted and white mesh instances of one run, a shader without a_color,
   * tinted and white skinned instances of one run. */
  const NOT_READY = 0x80000000;
  const probe = () =>
    page.evaluate(() => {
      const fn = (window as unknown as { __nt?: MeshHooks }).__nt?.mesh_color_probe;
      return fn ? fn() >>> 0 : -1;
    });
  let mask = NOT_READY;
  await expect.poll(async () => (mask = await probe())).not.toBe(NOT_READY);
  expect(mask).toBe(0x3f); /* -1: the probe hook is missing */
  expect(errors).toEqual([]);
});
