import { test, expect } from '@playwright/test';
import { execFileSync } from 'child_process';
import { join } from 'path';

// Needs devapi_host built with NT_DEVAPI_GROUP_RECORDER, plus python and ffmpeg: a lockstep record.*
// run, then the downloaded MP4 is checked for streams, frame count and flash/beep sync
// (tools/devapi/check_recording.py).
declare global {
  interface Window {
    __devapi?: { ready: boolean; submit(line: string): string };
  }
}

test('record.* in lockstep downloads a synced MP4', async ({ page }, testInfo) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__devapi?.ready === true, null, { timeout: 30_000 });
  const call = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(({ method, params }) => {
      const resp = window.__devapi!.submit(JSON.stringify({ method, params: params ?? {} }));
      return resp ? JSON.parse(resp) : null;
    }, { method, params });
  const step = async (count: number) => {
    const from = (await call('frame.current')).result.frame;
    if (count > 0) await call('time.step', { count });
    await expect.poll(async () => (await call('frame.current')).result.frame, { timeout: 60_000 }).toBeGreaterThanOrEqual(from + count);
  };

  expect((await call('time.set_mode', { mode: 'manual' })).ok).toBe(true);
  // Start half a second away from a flash, so no flash or beep is cut at either end of the clip.
  const time = (await call('frame.current')).result.time;
  await step(Math.round(((Math.floor(time) + 1.5 - time) % 1) * 60));
  // start fails until the codec probe settles, a moment after load.
  await expect.poll(async () => (await call('record.start', { name: 'probe.mp4', fps: 30 })).ok, { timeout: 30_000 }).toBe(true);
  expect((await call('record.start', { name: 'probe.mp4', fps: 30 })).error.code).toBe('busy');
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

// RUN mode: record.start lands inside a frame whose step already advanced time, and that step's samples
// are pushed after it. A step of ~117 ms (scale 7) would show any one-step offset as a drift WARN.
test('record.* in real time keeps audio on the clock', async ({ page }) => {
  const warnings: string[] = [];
  let started = false;
  page.on('console', (msg) => {
    started ||= /recording \d+x\d+ at 30 fps/.test(msg.text());
    if (/audio .* the clock/.test(msg.text())) warnings.push(msg.text());
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__devapi?.ready === true, null, { timeout: 30_000 });
  const call = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(({ method, params }) => {
      const resp = window.__devapi!.submit(JSON.stringify({ method, params: params ?? {} }));
      return resp ? JSON.parse(resp) : null;
    }, { method, params });
  expect((await call('time.set_scale', { scale: 7 })).ok).toBe(true);
  await expect.poll(async () => (await call('record.start', { name: 'run.mp4', fps: 30 })).ok, { timeout: 30_000 }).toBe(true);
  await page.waitForTimeout(1500);
  expect((await call('record.stop')).result.frames).toBeGreaterThan(30);
  expect(started).toBe(true); // the recorder's log reaches the console, so no WARN means no drift
  expect(warnings).toEqual([]);
});
