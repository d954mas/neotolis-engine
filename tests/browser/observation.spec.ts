import { test, expect } from '@playwright/test';

type ObserveHooks = {
  ready: boolean;
  programs_ready(): boolean;
  observe_probe(mode: number): number;
  observe_value(index: number): number;
  observe_record(enabled: number): void;
  observe_status(): number;
};
type CallControl = { active: boolean; calls: Record<string, number>; payloads: Record<string, number>; bytes: Record<string, number> };

test('gfx observation reconciles issued WebGL calls and preserves pixels on overflow', async ({ page }) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const control: CallControl = { active: false, calls: {}, payloads: {}, bytes: {} };
    (window as unknown as { observeControl: CallControl }).observeControl = control;
    const proto = WebGL2RenderingContext.prototype as unknown as Record<string, (...args: unknown[]) => unknown>;
    // Bytes the driver actually receives from a CPU payload; null when the call carries none.
    // Emscripten passes the whole heap plus an offset/length (WebGL2 overloads), so a view's
    // byteLength alone overstates the upload.
    const view = (value: unknown) => (ArrayBuffer.isView(value) ? value : null);
    const pixelBytes = (args: unknown[], w: number, h: number, format: number, type: number) => {
      const gl = WebGL2RenderingContext;
      if (format !== gl.RGBA || type !== gl.UNSIGNED_BYTE) throw new Error(`unhandled upload format ${format}/${type}`);
      return (args[w] as number) * (args[h] as number) * 4;
    };
    const payload: Record<string, (args: unknown[]) => number | null> = {
      bufferData: args => (!view(args[1]) ? null : args.length >= 5 ? (args[4] as number) : view(args[1])!.byteLength),
      bufferSubData: args => (!view(args[2]) ? null : args.length >= 5 ? (args[4] as number) : view(args[2])!.byteLength),
      texImage2D: args => (!view(args[8]) ? null : pixelBytes(args, 3, 4, args[6] as number, args[7] as number)),
      texSubImage2D: args => (!view(args[8]) ? null : pixelBytes(args, 4, 5, args[6] as number, args[7] as number)),
      compressedTexImage2D: args => (!view(args[6]) ? null : args.length >= 9 ? (args[8] as number) : view(args[6])!.byteLength),
    };
    for (const name of ['useProgram', 'bindVertexArray', 'bindTexture', 'bindSampler', 'uniform4fv', 'uniform1i', 'drawArrays', ...Object.keys(payload)]) {
      const original = proto[name];
      proto[name] = function (...args: unknown[]) {
        if (control.active) {
          control.calls[name] = (control.calls[name] || 0) + 1;
          const bytes = payload[name]?.(args);
          if (bytes !== undefined && bytes !== null) {
            control.payloads[name] = (control.payloads[name] || 0) + 1;
            control.bytes[name] = (control.bytes[name] || 0) + bytes;
          }
        }
        return original.apply(this, args);
      };
    }
  });
  await page.goto('/');
  await page.waitForFunction(() => {
    const hooks = (window as unknown as { __nt?: ObserveHooks }).__nt;
    return hooks?.ready && hooks.programs_ready();
  });
  const runs = await page.evaluate(() => {
    const hooks = (window as unknown as { __nt: ObserveHooks }).__nt;
    const control = (window as unknown as { observeControl: CallControl }).observeControl;
    return [0, 1, 2].map(mode => {
      control.calls = {};
      control.payloads = {};
      control.bytes = {};
      control.active = true;
      const pixel = hooks.observe_probe(mode) >>> 0;
      control.active = false;
      return {
        mode,
        pixel,
        values: Array.from({ length: 31 }, (_, i) => hooks.observe_value(i)),
        calls: { ...control.calls },
        payloads: { ...control.payloads },
        bytes: { ...control.bytes },
      };
    });
  });
  for (const run of runs) {
    const v = run.values;
    expect(run.pixel).toBe(0xffc08040);
    expect(v[1]).toBe(1); // capture compiled in
    expect(v[2]).toBe(1); // COMPLETE counters, independent of capture overflow.
    expect(v[3]).toBe(1);
    expect(v.slice(4, 6)).toEqual([16, 32]); // Preparation before gfx begin.
    expect(v.slice(14, 18)).toEqual([16, 32, 1, 2]);
    expect(v[6]).toBe(run.calls.useProgram || 0);
    expect(v[7]).toBe(run.calls.bindVertexArray || 0);
    expect(v[8]).toBe(run.calls.bindTexture || 0);
    expect(v[9]).toBe(run.calls.bindSampler || 0);
    expect(v[10]).toBe((run.calls.uniform4fv || 0) + (run.calls.uniform1i || 0));
    expect(run.calls.drawArrays).toBe(1);
    // Upload counters against the payloads the driver received, and gl[] against every call.
    const sum = (record: Record<string, number>, names: string[]) => names.reduce((total, name) => total + (record[name] || 0), 0);
    const buffers = ['bufferData', 'bufferSubData'];
    const textures = ['texImage2D', 'texSubImage2D', 'compressedTexImage2D'];
    expect(v[14]).toBe(sum(run.bytes, buffers));
    expect(v[15]).toBe(sum(run.bytes, textures));
    expect(v[16]).toBe(sum(run.payloads, buffers));
    expect(v[17]).toBe(sum(run.payloads, textures));
    expect(v.slice(26, 31)).toEqual([...buffers, ...textures].map(name => run.calls[name] || 0));
    if (run.mode === 1) {
      expect(v[11]).toBe(0);
      expect(v[12]).toBeGreaterThan(0);
      expect(v[13]).toBe(1);
      expect(v.slice(20, 26)).toEqual(['useProgram', 'bindVertexArray', 'bindTexture', 'bindSampler', 'uniform4fv', 'uniform1i'].map(name => run.calls[name] || 0));
    } else if (run.mode === 2) {
      expect(v[11]).toBe(1);
      expect(v[12]).toBe(16384);
      expect(v[13]).toBe(2); // TRUNCATED capture.
    }
  }
  expect(errors).toEqual([]);
});

test('gfx observation marks context loss aborted and resumes complete frames', async ({ page }) => {
  test.setTimeout(60_000);
  await page.goto('/');
  await page.waitForFunction(() => (window as unknown as { __nt?: ObserveHooks }).__nt?.programs_ready());
  await page.evaluate(() => {
    const hooks = (window as unknown as { __nt: ObserveHooks }).__nt;
    const gl = document.querySelector('canvas')!.getContext('webgl2')!;
    const loss = gl.getExtension('WEBGL_lose_context');
    if (!loss) throw new Error('WEBGL_lose_context unavailable');
    (window as unknown as { observationLoss: WEBGL_lose_context }).observationLoss = loss;
    hooks.observe_record(1);
    loss.loseContext();
  });
  await page.waitForFunction(() => (window as unknown as { __nt: ObserveHooks }).__nt.observe_status() === 3);
  await page.evaluate(() => (window as unknown as { observationLoss: WEBGL_lose_context }).observationLoss.restoreContext());
  await page.waitForFunction(() => {
    const hooks = (window as unknown as { __nt: ObserveHooks }).__nt;
    return hooks.observe_status() === 1 && hooks.programs_ready();
  });
});
