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
      http_get_slow_timeout(): void;
      http_get_slow(): void;
      http_state(): number; // nt_http_state_t: 3 = DONE, 4 = FAILED
      http_status(): number;
      http_total(): number;
      http_headers(): string;
      http_verify_echo(): number; // 1 exact, 0 mismatch, -1 not DONE
      http_free(): void;
    };
  }
}

const DONE = 3;
const FAILED = 4;

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

test('http: timeout aborts to FAILED; free mid-download cancels and the slot is reusable', async ({ page }) => {
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready === true, null, { timeout: 30_000 });

  // timeout_ms=300 against /slow (5 s stall): the AbortController path must end FAILED.
  await page.evaluate(() => window.__nt!.http_get_slow_timeout());
  await expect
    .poll(() => page.evaluate(() => window.__nt!.http_state()), { timeout: 10_000 })
    .toBe(FAILED);
  await page.evaluate(() => window.__nt!.http_free());

  // /slow sends its headers (Content-Length: 2) at once and stalls the body: once the headers
  // reached C as the progress total, the status still stays 0 until completion.
  await page.evaluate(() => window.__nt!.http_get_slow());
  await expect
    .poll(() => page.evaluate(() => window.__nt!.http_total()), { timeout: 10_000 })
    .toBe(2);
  expect(await page.evaluate(() => window.__nt!.http_state())).toBeLessThan(DONE);
  expect(await page.evaluate(() => window.__nt!.http_status())).toBe(0);

  // Free mid-flight and let frames pass: the frame pump must skip the freed slot.
  await page.evaluate(() => window.__nt!.http_free());
  await page.evaluate(() => new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r))));

  // Cancel and reuse the slot at once: the aborted fetch's late handlers must not touch the new request.
  await page.evaluate(() => {
    window.__nt!.http_get_slow();
    window.__nt!.http_free();
    window.__nt!.http_post_echo();
  });
  await expect
    .poll(() => page.evaluate(() => window.__nt!.http_state()), { timeout: 10_000 })
    .toBe(DONE);
  expect(await page.evaluate(() => window.__nt!.http_verify_echo())).toBe(1);
});
