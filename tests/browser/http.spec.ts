import { test, expect } from '@playwright/test';

// nt_http acceptance in a real Chromium (issue #384): the wasm build POSTs a binary body
// (with embedded NUL bytes) through nt_http_request_ex to the static server's /echo endpoint;
// the C side verifies the echoed body byte-exact. Also: response headers/status surfaced, and
// a non-2xx response completes as DONE with its status (not FAILED).
declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      http_post_echo(): void;
      http_get_404(): void;
      http_state(): number; // nt_http_state_t: 3 = DONE, 4 = FAILED
      http_status(): number;
      http_headers(): string;
      http_verify_echo(): number; // 1 exact, 0 mismatch, -1 not DONE
      http_free(): void;
    };
  }
}

const DONE = 3;

test('http: POST body arrives verbatim, headers/status surface, 404 is DONE', async ({ page }) => {
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });

  // POST /echo: method + body + Content-Type + custom header through fetch()
  await page.evaluate(() => window.__nt!.http_post_echo());
  await expect
    .poll(() => page.evaluate(() => window.__nt!.http_state()), { timeout: 10_000 })
    .toBe(DONE);

  expect(await page.evaluate(() => window.__nt!.http_status())).toBe(200);
  const headers = await page.evaluate(() => window.__nt!.http_headers());
  // The server echoes what it received back into response headers (lowercased names on our side).
  expect(headers).toContain('x-echo-content-type: application/json');
  expect(headers).toContain('x-echo-x-nt-test: neotolis');
  // Byte-exact echo verified in C (memcmp against the sent buffer).
  expect(await page.evaluate(() => window.__nt!.http_verify_echo())).toBe(1);

  // Non-2xx: a full response is DONE with its status — the caller decides.
  await page.evaluate(() => window.__nt!.http_get_404());
  await expect
    .poll(() => page.evaluate(() => window.__nt!.http_state()), { timeout: 10_000 })
    .toBe(DONE);
  expect(await page.evaluate(() => window.__nt!.http_status())).toBe(404);
  await page.evaluate(() => window.__nt!.http_free());
});
