import { test, expect } from '@playwright/test';
import { execFileSync } from 'child_process';
import { join } from 'path';

// Needs devapi_host built with NT_DEVAPI_GROUP_RECORDER (docs/build.md). The lockstep test also needs
// python and ffmpeg: tools/devapi/check_recording.py checks the downloaded MP4.
declare global {
  interface Window {
    __devapi?: { submit(line: string): string; poll(): string };
  }
}

test('record.* in lockstep downloads a synced MP4', async ({ page }, testInfo) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__devapi !== undefined, null, { timeout: 30_000 });
  const call = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(({ method, params }) => {
      const resp = window.__devapi!.submit(JSON.stringify({ method, params: params ?? {} }));
      return resp ? JSON.parse(resp) : null;
    }, { method, params });
  // time.step replies once its steps are done; until that reply is drained every other command is busy.
  const step = async (count: number) => {
    if (count <= 0) return;
    expect(await call('time.step', { count })).toBeNull();
    let line = '';
    await expect.poll(async () => (line = await page.evaluate(() => window.__devapi!.poll())), { timeout: 60_000 }).not.toBe('');
    expect(JSON.parse(line).ok).toBe(true);
  };

  expect((await call('time.set_mode', { mode: 'manual' })).ok).toBe(true);
  // Start half a second away from a flash, so no flash or beep is cut at either end of the clip.
  const time = (await call('frame.current')).result.time;
  await step(Math.round(((Math.floor(time) + 1.5 - time) % 1) * 60));
  // start fails until the codec probe settles, a moment after load.
  await expect.poll(async () => (await call('record.start', { name: 'probe.mp4', fps: 30 })).ok, { timeout: 30_000 }).toBe(true);
  expect((await call('record.start', { name: 'probe.mp4', fps: 30 })).error.code).toBe('recording');
  // 360 steps of the default 1/60 at 30 fps: 180 frames, a flash and a beep each game second.
  await step(360);
  const download = page.waitForEvent('download');
  expect((await call('record.stop')).result.frames).toBe(180);
  expect((await call('record.stop')).result.frames).toBe(0);

  const file = await download;
  expect(file.suggestedFilename()).toBe('probe.mp4');
  const mp4 = testInfo.outputPath('probe.mp4');
  await file.saveAs(mp4);
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const out = execFileSync(python, [join(__dirname, '..', '..', 'tools', 'devapi', 'check_recording.py'), mp4, '--fps', '30', '--frames', '180']);
  console.log(out.toString().trim());
  expect(errors).toEqual([]);
});

// RUN mode: a step of ~117 ms (scale 7) would show a one-step offset between record.start and the
// pushed stream as a drift WARN. Frames reach the encoder at recording fps x scale per second, so a
// small canvas keeps a software encoder (CI) well inside its backlog bound.
test('record.* in real time keeps audio on the clock', async ({ page }) => {
  await page.setViewportSize({ width: 320, height: 180 });
  const warnings: string[] = [];
  const log: string[] = [];
  page.on('console', (msg) => {
    if (msg.text().includes('[recorder]')) log.push(msg.text());
    if (/audio .* the clock/.test(msg.text())) warnings.push(msg.text());
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__devapi !== undefined, null, { timeout: 30_000 });
  const call = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(({ method, params }) => {
      const resp = window.__devapi!.submit(JSON.stringify({ method, params: params ?? {} }));
      return resp ? JSON.parse(resp) : null;
    }, { method, params });
  for (const params of [{ name: 'run.mp4', fps: 0 }, { name: 'run.mp4', fps: 121 }, { name: '', fps: 5 }]) {
    expect((await call('record.start', params)).error.code).toBe('bad_params');
  }
  // A host that takes the blob cancels the event, so nothing downloads.
  let downloads = 0;
  page.on('download', () => downloads++);
  await page.evaluate(() => window.addEventListener('nt-recorder', (e) => {
    e.preventDefault();
    const d = (e as CustomEvent).detail;
    (window as any).__rec = { size: d.blob ? d.blob.size : 0, error: d.error };
  }));
  expect((await call('time.set_scale', { scale: 7 })).ok).toBe(true);
  await expect.poll(async () => (await call('record.start', { name: 'run.mp4', fps: 5 })).ok, { timeout: 30_000 }).toBe(true);
  const from = (await call('frame.current')).result;
  await page.waitForTimeout(1500);
  const to = (await call('frame.current')).result;
  const frames = (await call('record.stop')).result.frames;
  // The steps must exceed the drift bound, or a one-step offset would pass unseen.
  expect((to.time - from.time) / (to.frame - from.frame)).toBeGreaterThan(0.1);
  console.log(log.join('\n'));
  expect(frames).toBeGreaterThan(5);
  const rec = await page.waitForFunction(() => (window as any).__rec, null, { timeout: 30_000 }).then((h) => h.jsonValue());
  expect(rec.error).toBeNull();
  expect(rec.size).toBeGreaterThan(0);
  await page.waitForTimeout(2000); // a download reaches Playwright some time after the click
  expect(downloads).toBe(0);
  expect(log.some((line) => / at 5 fps/.test(line))).toBe(true); // the log reaches the console, so no WARN means no drift
  expect(warnings).toEqual([]);
});
