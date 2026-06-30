import { test, expect } from '@playwright/test';

// The web devapi bridge the capture-build devapi_host installs (engine/devapi/nt_devapi_web.c,
// EM_JS): submit(line) returns the synchronous response line ("" when the command deferred — C
// returned NULL), poll() returns a queued deferred response line ("" when none ready), tick(n)
// submits a time.step{count:n}, ready flips true after install. No listener on web.
declare global {
  interface Window {
    __devapi?: {
      ready: boolean;
      submit(line: string): string;
      poll(): string;
      tick(n?: number): string;
    };
  }
}

// Monotonic request_id allocator so a deferred capture's DATA line is matched by id, never by
// arrival order (an unrelated line must not be mistaken for the capture result).
let nextId = 1;

// Web-smoke gate: only a real headless Chromium exposes the deferred-capture drain race — the PNG
// resolves to real pixels (not {deferred:true}) only after a rendered frame fills the pre-swap seam.
test('devapi web transport: discovery + game.* + deferred capture yields a non-blank PNG', async ({ page }) => {
  await page.goto('/index.html');

  // Frame barrier: resolve after two nested requestAnimationFrame calls — one real rendered frame.
  // The deferred capture's pre-swap producer only fills its payload INSIDE the swap this schedules
  // (a render-disabled frame won't fill it).
  const frame = () =>
    page.evaluate(() => new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r()))));

  // Boot gate: the web shim sets window.__devapi.ready once installed (engine/devapi/nt_devapi_web.c).
  await page.waitForFunction(() => window.__devapi?.ready === true, null, { timeout: 30_000 });

  // submit one method+params line, parse the synchronous response envelope. A "" response means the
  // command DEFERRED — the caller must poll() for the data line (used by the capture path below).
  const submit = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(
      ({ method, params, rid }) => {
        const line = JSON.stringify(params ? { method, request_id: rid, params } : { method, request_id: rid });
        const resp = window.__devapi!.submit(line);
        return resp ? JSON.parse(resp) : null;
      },
      { method, params, rid: nextId++ },
    );

  // 1. Discovery — endpoints lists BOTH game commands (zero-engine-edit registration).
  const ping = await submit('ping');
  expect(ping?.ok).toBe(true);
  const eps = await submit('endpoints');
  const methods: string[] = (eps?.result?.commands ?? []).map((c: { method: string }) => c.method);
  expect(methods).toContain('game.echo');
  expect(methods).toContain('game.poke');

  // 2. game.* round-trip — echo read-back + poke write flips the observable host bool.
  const echoed = await submit('game.echo', { msg: 'hi' });
  expect(echoed?.ok).toBe(true);
  expect(echoed?.result?.msg).toBe('hi');
  const poke1 = await submit('game.poke');
  const poke2 = await submit('game.poke');
  expect(Boolean(poke2?.result?.poked)).toBe(!Boolean(poke1?.result?.poked));

  // 3. Deterministic pump + the riskiest web path: a deferred capture.frame -> real PNG.
  //    MANUAL mode makes progress deterministic; tick(n) + frame() advances RENDERED frames so the
  //    pre-swap seam fills the payload before we poll.
  await submit('time.set_mode', { mode: 'manual' });

  // Submit capture.frame with a known request_id; the web host DEFERS it (submit returns ""), so the
  // PNG arrives on a later poll() AFTER a rendered frame — match it by request_id.
  const captureId = nextId++;
  const submitted = await page.evaluate((rid) => {
    const line = JSON.stringify({ method: 'capture.frame', request_id: rid });
    return window.__devapi!.submit(line); // "" when deferred (the expected web path)
  }, captureId);

  // The deferred data line arrives only after a rendered frame fills the pre-swap seam. Drive frames
  // (tick advances MANUAL sim time; frame() forces a real rAF render+swap) and poll until the matching
  // id appears, bounded by expect.poll's timeout so a deferred-forever capture never hangs CI.
  let captureResp: { ok?: boolean; result?: { format?: string; data?: string; deferred?: boolean } } | null = null;
  if (submitted) {
    // Non-deferred (a capture-OFF host would answer synchronously) — accept the immediate envelope.
    captureResp = JSON.parse(submitted);
  } else {
    // poll() DEQUEUES, so the matching line is returned exactly once — keep it the instant it appears (a
    // second poll for the same id returns "" because the queue already drained it). Assert on the kept
    // line, never a re-drive (the re-drive would always see an empty queue).
    let captureLine = '';
    await expect
      .poll(
        async () => {
          await page.evaluate(() => window.__devapi!.tick(1)); // advance one MANUAL sim step
          await frame(); // force a real rendered frame -> pre-swap seam fills the payload
          const s = await page.evaluate((rid) => {
            // Drain queued deferred lines; return the one whose request_id matches the capture.
            for (let i = 0; i < 8; i++) {
              const line = window.__devapi!.poll();
              if (!line) break;
              const obj = JSON.parse(line);
              if (obj.request_id === rid) return line;
            }
            return '';
          }, captureId);
          if (s) captureLine = s; // keep the first matching line; poll() won't return it again
          return captureLine;
        },
        { timeout: 20_000 },
      )
      .not.toBe('');
    captureResp = captureLine ? JSON.parse(captureLine) : null;
  }

  // The deferred capture must be a real PNG, not {deferred:true}, with a non-empty base64 payload.
  expect(captureResp?.ok).toBe(true);
  expect(captureResp?.result?.deferred).not.toBe(true);
  expect(captureResp?.result?.format).toBe('png');
  const b64 = captureResp?.result?.data ?? '';
  expect(b64.length).toBeGreaterThan(0);

  // Decode the PNG in-page and assert it is genuinely NON-BLANK: the host renders a two-tone frame, so
  // a uniform/blank capture (a real seam failure) yields a single colour and fails here, not just "has bytes".
  const distinctColors = await page.evaluate(async (data) => {
    const img = new Image();
    await new Promise<void>((res, rej) => {
      img.onload = () => res();
      img.onerror = () => rej(new Error('png decode failed'));
      img.src = 'data:image/png;base64,' + data;
    });
    const cv = document.createElement('canvas');
    cv.width = img.width;
    cv.height = img.height;
    const ctx = cv.getContext('2d')!;
    ctx.drawImage(img, 0, 0);
    const px = ctx.getImageData(0, 0, cv.width, cv.height).data;
    const seen = new Set<number>();
    for (let i = 0; i < px.length; i += 4 * 37) {
      seen.add((px[i] << 16) | (px[i + 1] << 8) | px[i + 2]);
      if (seen.size > 1) break;
    }
    return seen.size;
  }, b64);
  expect(distinctColors).toBeGreaterThan(1);
});
