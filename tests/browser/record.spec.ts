import { test, expect } from '@playwright/test';
import { execFileSync } from 'child_process';
import { writeFileSync } from 'fs';
import { join } from 'path';

// devapi_host built with NT_DEVAPI_GROUP_RECORDER: a lockstep record.* run, then the MP4 from the
// 'nt-recorder' event is checked for streams, frame count and flash/beep sync
// (tools/devapi/check_recording.py, needs ffmpeg).
declare global {
  interface Window {
    __devapi?: { ready: boolean; submit(line: string): string; poll(): string };
    __rec?: { name: string; frames: number; error: string | null; b64?: string };
    NtRecorder?: { ready(): boolean };
  }
}

test('record.* in lockstep writes a synced MP4', async ({ page }, testInfo) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  await page.addInitScript(() => {
    window.addEventListener('nt-recorder', (e) => {
      e.preventDefault();
      const d = (e as CustomEvent).detail;
      if (!d.blob) {
        window.__rec = { name: d.name, frames: d.frames, error: d.error };
        return;
      }
      d.blob.arrayBuffer().then((buf: ArrayBuffer) => {
        const u = new Uint8Array(buf);
        let s = '';
        for (let k = 0; k < u.length; k += 0x8000) s += String.fromCharCode(...u.subarray(k, k + 0x8000));
        window.__rec = { name: d.name, frames: d.frames, error: d.error, b64: btoa(s) };
      });
    });
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__devapi?.ready === true, null, { timeout: 30_000 });
  const call = (method: string, params?: Record<string, unknown>) =>
    page.evaluate(({ method, params }) => {
      const resp = window.__devapi!.submit(JSON.stringify({ method, params: params ?? {} }));
      return resp ? JSON.parse(resp) : null;
    }, { method, params });

  expect((await call('time.set_mode', { mode: 'manual' })).ok).toBe(true);
  await page.waitForFunction(() => window.NtRecorder?.ready() === true, null, { timeout: 30_000 });
  const step = async (count: number) => {
    const from = (await call('frame.current')).result.frame;
    if (count > 0) await call('time.step', { count });
    await expect.poll(async () => (await call('frame.current')).result.frame, { timeout: 60_000 }).toBeGreaterThanOrEqual(from + count);
  };
  // Start half a second away from a flash, so no flash or beep is cut at either end of the clip.
  const time = (await call('frame.current')).result.time;
  await step(Math.round(((Math.floor(time) + 1.5 - time) % 1) * 60));
  expect((await call('record.start', { name: 'probe.mp4', fps: 30 })).ok).toBe(true);
  expect((await call('record.start', { name: 'probe.mp4', fps: 30 })).error.code).toBe('busy');
  // 360 steps of the default 1/60 at 30 fps: 180 frames, a flash and a beep each game second.
  await step(360);
  expect((await call('record.stop')).result.frames).toBe(180);
  expect((await call('record.stop')).result.frames).toBe(0);

  await page.waitForFunction(() => window.__rec !== undefined, null, { timeout: 30_000 });
  const rec = await page.evaluate(() => window.__rec!);
  expect(rec.error).toBeNull();
  expect(rec.frames).toBe(180);
  const mp4 = testInfo.outputPath('probe.mp4');
  writeFileSync(mp4, Buffer.from(rec.b64!, 'base64'));
  const python = process.env.PYTHON || (process.platform === 'win32' ? 'python' : 'python3');
  const out = execFileSync(python, [join(__dirname, '..', '..', 'tools', 'devapi', 'check_recording.py'), mp4, '--fps', '30', '--frames', '180']);
  console.log(out.toString().trim());
  expect(errors).toEqual([]);
});
