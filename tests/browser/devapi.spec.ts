import { test, expect } from '@playwright/test';

// The web devapi bridge the capture-build devapi_host installs (engine/devapi/nt_devapi_web.c,
// EM_JS): submit(line) returns the synchronous response line ("" when the command deferred — C
// returned NULL), poll() returns a queued deferred response line ("" when none ready), tick(n)
// submits a time.step{count:n}, ready flips true after install. No listener on web (D-16).
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
// arrival order (an unrelated line must not be mistaken for the capture result — T-70-03-RACE).
let nextId = 1;

// FIRST web-smoke gate for the devapi web transport: a real headless Chromium drives the genuine
// window.__devapi bridge (submit/poll/tick) against the capture-build devapi_host wasm, exercising
// discovery + the two game commands + the riskiest web path — a deferred capture.frame that must
// resolve to a real PNG (not {deferred:true}) only AFTER a rendered frame fills the pre-swap seam
// (Pitfall 3, the drain-race unit tests cannot see). The full live run is the orchestrator's
// mandatory Wave-4 UAT; this is its test body.
test('devapi web transport: discovery + game.* + deferred capture yields a non-blank PNG', async ({ page }) => {
  await page.goto('/index.html');

  // Frame barrier: resolve after two nested requestAnimationFrame calls — one real rendered frame.
  // The deferred capture's pre-swap producer only fills its payload INSIDE the swap this schedules
  // (Pitfall 3/4: a render-disabled frame won't fill it). Mirrors tests/browser/input.spec.ts:26.
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

  // 1. Discovery — endpoints lists BOTH game commands (zero-engine-edit registration, HARNESS-05).
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

  // 3. Deterministic pump (D-02) + the riskiest web path: a deferred capture.frame -> real PNG.
  //    MANUAL mode makes progress deterministic; tick(n) + frame() advances RENDERED frames so the
  //    pre-swap seam fills the payload before we poll (Pitfall 3).
  await submit('time.set_mode', { mode: 'manual' });

  // Submit capture.frame with a known request_id; the web host DEFERS it (submit returns ""), so the
  // PNG arrives on a later poll() AFTER a rendered frame — match it by request_id (T-70-03-RACE).
  const captureId = nextId++;
  const submitted = await page.evaluate((rid) => {
    const line = JSON.stringify({ method: 'capture.frame', request_id: rid });
    return window.__devapi!.submit(line); // "" when deferred (the expected web path)
  }, captureId);

  // The deferred data line arrives only after a rendered frame fills the pre-swap seam. Drive frames
  // (tick advances MANUAL sim time; frame() forces a real rAF render+swap) and poll until the matching
  // id appears, bounded by expect.poll's timeout so a deferred-forever capture never hangs CI (D-01).
  let captureResp: { ok?: boolean; result?: { format?: string; data?: string; deferred?: boolean } } | null = null;
  if (submitted) {
    // Non-deferred (a capture-OFF host would answer synchronously) — accept the immediate envelope.
    captureResp = JSON.parse(submitted);
  } else {
    await expect
      .poll(
        async () => {
          await page.evaluate(() => window.__devapi!.tick(1)); // advance one MANUAL sim step
          await frame(); // force a real rendered frame -> pre-swap seam fills the payload
          return page.evaluate((rid) => {
            // Drain queued deferred lines; return the one whose request_id matches the capture.
            for (let i = 0; i < 8; i++) {
              const s = window.__devapi!.poll();
              if (!s) break;
              const obj = JSON.parse(s);
              if (obj.request_id === rid) return s;
            }
            return '';
          }, captureId);
        },
        { timeout: 20_000 },
      )
      .not.toBe('');
    // Re-drive once more to fetch the matched line for assertion (the poll above proved it arrives).
    const line = await page.evaluate(async (rid) => {
      for (let attempt = 0; attempt < 30; attempt++) {
        await new Promise<void>((r) => requestAnimationFrame(() => requestAnimationFrame(() => r())));
        for (let i = 0; i < 8; i++) {
          const s = window.__devapi!.poll();
          if (!s) break;
          const obj = JSON.parse(s);
          if (obj.request_id === rid) return s;
        }
      }
      return '';
    }, captureId);
    captureResp = line ? JSON.parse(line) : null;
  }

  // CAP web half / Pitfall 3: the deferred capture must be a real PNG, NOT {deferred:true}, with a
  // non-empty base64 payload (the deep pixel decode is the Python capture path / the orchestrator UAT).
  expect(captureResp?.ok).toBe(true);
  expect(captureResp?.result?.deferred).not.toBe(true);
  expect(captureResp?.result?.format).toBe('png');
  expect((captureResp?.result?.data ?? '').length).toBeGreaterThan(0);
});
