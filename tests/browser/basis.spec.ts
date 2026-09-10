import { test, expect } from '@playwright/test';

test('fresh Basis LDR assets convert and sample RGB, alpha and every mip', async ({ page }, testInfo) => {
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
  const pack = page.waitForResponse(response => response.url().endsWith('/assets/fixture.ntpack'));
  await page.goto('/');
  expect((await pack).status()).toBe(200);
  await expect.poll(async () => {
    expect(errors).toEqual([]);
    return page.evaluate(() => !!(window as any).basisResult);
  }, { timeout: 20_000 }).toBe(true);
  const result = await page.evaluate(() => {
    const canvas = document.querySelector('canvas')!;
    const gl = canvas.getContext('webgl2')!;
    const debug = gl.getExtension('WEBGL_debug_renderer_info');
    return { ...(window as any).basisResult, renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER), glError: gl.getError() };
  });
  expect(errors).toEqual([]);
  expect(result.preset).toBe(process.env.NT_BASIS_PRESET);
  expect(result.assertMode).toBe(process.env.NT_BASIS_PRESET!.includes('release') ? 1 : 2);
  expect(result.cpuRows).toBe(140);
  expect(result.gpuRows).toBe(28);
  expect(result.glError).toBe(0);
  if (process.env.NT_BASIS_HARDWARE === '1') expect(result.renderer).not.toMatch(/swiftshader|llvmpipe|software/i);
  await testInfo.attach('basis-result', { body: JSON.stringify(result, null, 2), contentType: 'application/json' });
  console.log(JSON.stringify(result));
});
