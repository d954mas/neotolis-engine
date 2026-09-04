import { test, expect } from '@playwright/test';

declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      semantic_binding_ok(): boolean;
    };
  }
}

test('semantic texture bindings map reversed names to their linked units', async ({ page }) => {
  await page.goto('/');
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });
  await expect.poll(() => page.evaluate(() => window.__nt!.semantic_binding_ok()), { timeout: 10_000 }).toBe(true);
});
