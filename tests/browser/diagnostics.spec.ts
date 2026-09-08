import { test, expect, type Page } from '@playwright/test';

type DiagnosticsHooks = {
  ready: boolean;
  programs_ready(): boolean;
  drawn_frames(): number;
  diagnostics_config(): { preset: string; log: number; ui: number; gpu: number; metrics: number };
  gpu_supported(): boolean;
  float_probe(useTexture: number): number;
  gpu_command(operation: number, segment?: number): number;
};
type TimerCalls = { create: number; remove: number; begin: number; end: number; available: number; result: number; disjoint: number };
type TimerControl = {
  calls: TimerCalls;
  extensions: string[];
  disjoint: boolean;
  supported: boolean;
  hold: boolean;
  reset(): void;
};
declare global {
  interface Window {
    __ntTimerControl: TimerControl;
    __ntTimerLoss?: WEBGL_lose_context;
  }
}

const expected = {
  preset: process.env.NT_DIAGNOSTICS_PRESET ?? 'wasm-debug',
  log: Number(process.env.NT_DIAGNOSTICS_LOG ?? 0),
  ui: Number(process.env.NT_DIAGNOSTICS_UI ?? 1),
  gpu: Number(process.env.NT_DIAGNOSTICS_GPU ?? 1),
  metrics: Number(process.env.NT_DIAGNOSTICS_METRICS ?? 1),
};
const zero: TimerCalls = { create: 0, remove: 0, begin: 0, end: 0, available: 0, result: 0, disjoint: 0 };
// Emscripten 4.0.19 SAFE_HEAP rejects the u64 helper's low-word assignment above 32 bits.
// Release matrix selects a value above 32 bits to verify the full bridge separately.
const timerBase = Number(process.env.NT_DIAGNOSTICS_TIMER_BASE ?? 1_000_000);

async function installTimers(page: Page, supported = true): Promise<void> {
  await page.addInitScript(({ supported, timerBase }) => {
    const proto = WebGL2RenderingContext.prototype;
    const elapsed = 0x88bf;
    const gpuDisjoint = 0x8fbb;
    type Query = { value: number; ready: boolean; generation: number };
    const queries = new WeakMap<WebGLQuery, Query>();
    let active: Query | undefined;
    let serial = 0;
    const control: TimerControl = {
      calls: { create: 0, remove: 0, begin: 0, end: 0, available: 0, result: 0, disjoint: 0 },
      extensions: [], disjoint: false, supported, hold: false,
      reset() { for (const key of Object.keys(this.calls) as (keyof TimerCalls)[]) this.calls[key] = 0; },
    };
    window.__ntTimerControl = control;
    const getExtension = proto.getExtension;
    proto.getExtension = function(name: string) {
      control.extensions.push(name);
      if (/disjoint_timer_query/.test(name)) {
        return control.supported ? { TIME_ELAPSED_EXT: elapsed, GPU_DISJOINT_EXT: gpuDisjoint } : null;
      }
      return getExtension.call(this, name);
    };
    const getSupported = proto.getSupportedExtensions;
    proto.getSupportedExtensions = function() {
      const names = getSupported.call(this);
      if (names && control.supported && !names.includes('EXT_disjoint_timer_query_webgl2')) names.push('EXT_disjoint_timer_query_webgl2');
      return names;
    };
    const create = proto.createQuery;
    proto.createQuery = function() {
      const query = create.call(this);
      if (query) {
        control.calls.create++;
        queries.set(query, { value: 0, ready: false, generation: 0 });
      }
      return query;
    };
    const remove = proto.deleteQuery;
    proto.deleteQuery = function(query) {
      if (query && queries.has(query)) control.calls.remove++;
      remove.call(this, query);
    };
    const begin = proto.beginQuery;
    proto.beginQuery = function(target, query) {
      if (target !== elapsed) return begin.call(this, target, query);
      if (active) throw new Error('nested TIME_ELAPSED query');
      active = queries.get(query);
      if (!active) throw new Error('unknown TIME_ELAPSED query');
      active.ready = false;
      active.generation++;
      active.value = timerBase + ++serial;
      control.calls.begin++;
    };
    const end = proto.endQuery;
    proto.endQuery = function(target) {
      if (target !== elapsed) return end.call(this, target);
      if (!active) throw new Error('end without active TIME_ELAPSED query');
      const query = active;
      const generation = query.generation;
      active = undefined;
      control.calls.end++;
      // WebGL cannot publish a newly ended query within this browser task.
      setTimeout(() => { if (query.generation === generation) query.ready = true; }, 0);
    };
    const getQuery = proto.getQueryParameter;
    proto.getQueryParameter = function(query, pname) {
      const value = queries.get(query);
      if (!value || value.generation === 0) return getQuery.call(this, query, pname);
      if (pname === this.QUERY_RESULT_AVAILABLE) {
        control.calls.available++;
        return value.ready && !control.hold;
      }
      if (pname === this.QUERY_RESULT) {
        if (!value.ready || control.hold) throw new Error('blocking timer read before availability');
        control.calls.result++;
        return value.value;
      }
      return getQuery.call(this, query, pname);
    };
    const getParameter = proto.getParameter;
    proto.getParameter = function(pname) {
      if (pname !== gpuDisjoint) return getParameter.call(this, pname);
      control.calls.disjoint++;
      const disjoint = control.disjoint;
      control.disjoint = false;
      return disjoint;
    };
    document.addEventListener('webglcontextlost', () => { active = undefined; }, true);
  }, { supported, timerBase });
}

async function ready(page: Page): Promise<void> {
  await page.goto('/index.html');
  await page.waitForFunction(() => {
    const api = (window as unknown as { __nt?: DiagnosticsHooks }).__nt;
    return api?.ready && api.programs_ready() && api.drawn_frames() > 2;
  }, null, { timeout: 30_000 });
  expect(await page.evaluate(() => (window as unknown as { __nt: DiagnosticsHooks }).__nt.diagnostics_config()), 'served artifact must match the selected matrix entry').toEqual(expected);
}

test('diagnostics: selected producer controls extension activation and idle frames', async ({ page }) => {
  await installTimers(page);
  await ready(page);
  const observed = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    const control = window.__ntTimerControl;
    control.reset();
    api.gpu_command(4);
    api.gpu_command(4);
    const idle = { ...control.calls };
    api.gpu_command(2);
    control.reset();
    for (let i = 0; i < 3; i++) {
      api.gpu_command(0, i);
      api.gpu_command(1);
      api.gpu_command(4);
      api.gpu_command(5, i);
    }
    return { supported: api.gpu_supported(), extensions: control.extensions, idle, off: control.calls };
  });
  expect(observed.supported).toBe(expected.gpu !== 0);
  expect(observed.extensions.filter(name => /disjoint_timer_query/.test(name)).length).toBe(expected.gpu ? 1 : 0);
  for (const name of ['WEBGL_compressed_texture_astc', 'EXT_texture_compression_bptc', 'WEBGL_compressed_texture_etc', 'EXT_color_buffer_float', 'OES_texture_float_linear']) {
    expect(observed.extensions, 'explicit non-timer capability probe remains active').toContain(name);
  }
  expect(observed.idle).toEqual(zero);
  expect(observed.off).toEqual(zero);
});

for (const [mode, name] of [[0, 'RGBA16F render target'], [1, 'RGBA32F linear texture']] as const) {
  test(`diagnostics: explicit extensions preserve ${name} pixels`, async ({ page }) => {
    await installTimers(page);
    await ready(page);
    const result = await page.evaluate((mode) => (window as unknown as { __nt: DiagnosticsHooks }).__nt.float_probe(mode), mode);
    test.skip(result === -1, `${name} capability unavailable on this browser; functional path unverified`);
    expect(result, 'float path must create, sample and read back its authored pixel').toBeGreaterThanOrEqual(0);
    expect(Math.abs((result & 255) - 64)).toBeLessThanOrEqual(1);
    expect(Math.abs(((result >>> 8) & 255) - 128)).toBeLessThanOrEqual(1);
    expect(Math.abs(((result >>> 16) & 255) - 191)).toBeLessThanOrEqual(1);
  });
}

test('diagnostics: runtime disable cancels active and pending queries without readback', async ({ page }) => {
  test.skip(expected.gpu === 0, 'runtime transitions require the compiled producer');
  await installTimers(page);
  await ready(page);
  const cancelled = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    const control = window.__ntTimerControl;
    api.gpu_command(0, 0);
    api.gpu_command(1);
    api.gpu_command(0, 1);
    control.reset();
    api.gpu_command(2);
    api.gpu_command(1);
    api.gpu_command(2);
    api.gpu_command(4);
    const a = api.gpu_command(5, 0);
    const b = api.gpu_command(5, 1);
    const calls = { ...control.calls };
    api.gpu_command(3);
    return { a, b, calls, afterReenable: api.gpu_command(5, 0) };
  });
  expect(cancelled).toEqual({ a: -1, b: -1, calls: { ...zero, end: 1 }, afterReenable: -1 });
  const sameTask = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    api.gpu_command(0, 0);
    api.gpu_command(1);
    return api.gpu_command(5, 0);
  });
  expect(sameTask, 'new WebGL query must remain unavailable within its issuing task').toBe(-1);
  await expect.poll(() => page.evaluate(() => (window as unknown as { __nt: DiagnosticsHooks }).__nt.gpu_command(5, 0))).toBe(timerBase + 3);
  const drained = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    window.__ntTimerControl.reset();
    api.gpu_command(4);
    return { result: api.gpu_command(5, 0), calls: window.__ntTimerControl.calls };
  });
  expect(drained).toEqual({ result: -1, calls: zero });
});

test('diagnostics: disjoint cancels multiple pending segments and an active cross-frame query', async ({ page }) => {
  test.skip(expected.gpu === 0, 'disjoint requires the compiled producer');
  await installTimers(page);
  await ready(page);
  const observed = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    const control = window.__ntTimerControl;
    for (const id of [0, 1]) {
      api.gpu_command(0, id);
      api.gpu_command(1);
    }
    api.gpu_command(0, 2);
    control.reset();
    control.disjoint = true;
    api.gpu_command(4);
    api.gpu_command(1);
    const polls = [0, 1, 2].map(id => api.gpu_command(5, id));
    api.gpu_command(4);
    return { polls, calls: control.calls };
  });
  expect(observed).toEqual({ polls: [-1, -1, -1], calls: { ...zero, end: 1, disjoint: 1 } });
});

test('diagnostics: full ring stays nonblocking and pending full ring is disjoint work', async ({ page }) => {
  test.skip(expected.gpu === 0, 'rings require the compiled producer');
  await installTimers(page);
  await ready(page);
  const observed = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    const control = window.__ntTimerControl;
    control.hold = true;
    for (let i = 0; i < 8; i++) { api.gpu_command(0); api.gpu_command(1); }
    control.reset();
    control.disjoint = true;
    api.gpu_command(4);
    const fullDisjoint = { ...control.calls };
    const invalidated = api.gpu_command(5);
    control.reset();
    for (let i = 0; i < 9; i++) { api.gpu_command(0); api.gpu_command(1); }
    return { fullDisjoint, invalidated, overflow: control.calls };
  });
  expect(observed.fullDisjoint).toEqual({ ...zero, disjoint: 1 });
  expect(observed.invalidated).toBe(-1);
  expect(observed.overflow).toEqual({ ...zero, begin: 9, end: 9, available: 1 });
});

test('diagnostics: unsupported timer capability performs no timer work', async ({ page }) => {
  await installTimers(page, false);
  await ready(page);
  const observed = await page.evaluate(() => {
    const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
    const control = window.__ntTimerControl;
    control.reset();
    api.gpu_command(0);
    api.gpu_command(1);
    api.gpu_command(4);
    api.gpu_command(2);
    api.gpu_command(3);
    return { supported: api.gpu_supported(), result: api.gpu_command(5), calls: control.calls };
  });
  expect(observed).toEqual({ supported: false, result: -1, calls: zero });
});

test('diagnostics: latched disjoint after idle or re-enable discards the first pending sample', async ({ page }) => {
  test.skip(expected.gpu === 0, 'disjoint requires the compiled producer');
  await installTimers(page);
  await ready(page);
  for (const reenable of [false, true]) {
    const observed = await page.evaluate((reenable) => {
      const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
      const control = window.__ntTimerControl;
      if (reenable) api.gpu_command(2);
      control.disjoint = true;
      control.reset();
      api.gpu_command(4);
      const idle = { ...control.calls };
      if (reenable) api.gpu_command(3);
      api.gpu_command(0);
      api.gpu_command(1);
      const sameTask = api.gpu_command(5);
      api.gpu_command(4);
      const afterDisjoint = api.gpu_command(5);
      return { idle, sameTask, afterDisjoint, disjointReads: control.calls.disjoint };
    }, reenable);
    expect(observed).toEqual({ idle: zero, sameTask: -1, afterDisjoint: -1, disjointReads: 1 });
  }
});

test('diagnostics: loss cancels dead queries and restore preserves OFF and reprobes capabilities', async ({ page }) => {
  await installTimers(page);
  await ready(page);
  for (const supportedAfterRestore of [false, true]) {
    const stopped = await page.evaluate((supported) => {
      const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
      const control = window.__ntTimerControl;
      api.gpu_command(0, 0);
      api.gpu_command(1);
      api.gpu_command(0, 1);
      const gl = document.querySelector('canvas')!.getContext('webgl2')!;
      const extension = gl.getExtension('WEBGL_lose_context');
      if (!extension) throw new Error('WEBGL_lose_context unavailable');
      window.__ntTimerLoss = extension;
      control.supported = supported;
      control.reset();
      extension.loseContext();
      api.gpu_command(2);
      return control.calls;
    }, supportedAfterRestore);
    expect(stopped, 'raw loss setter must not end or delete dead queries').toEqual(zero);
    await page.waitForFunction(() => {
      const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
      return document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !api.programs_ready();
    });
    expect(await page.evaluate(() => window.__ntTimerControl.calls), 'processed loss forgets names without GL operations').toEqual(zero);
    await page.evaluate(() => {
      window.__ntTimerLoss!.restoreContext();
    });
    await page.waitForFunction(() => (window as unknown as { __nt: DiagnosticsHooks }).__nt.programs_ready(), null, { timeout: 30_000 });
    const restored = await page.evaluate(() => {
      const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
      const control = window.__ntTimerControl;
      const supported = api.gpu_supported();
      control.reset();
      api.gpu_command(0);
      api.gpu_command(1);
      api.gpu_command(4);
      const whileOff = { ...control.calls };
      const old = api.gpu_command(5);
      api.gpu_command(3);
      api.gpu_command(0);
      api.gpu_command(1);
      const resumed = { ...control.calls };
      api.gpu_command(2);
      return { supported, whileOff, old, resumed, timerExtensions: control.extensions.filter(name => /disjoint_timer_query/.test(name)).length };
    });
    const supported = expected.gpu !== 0 && supportedAfterRestore;
    expect(restored.supported).toBe(supported);
    expect(restored.whileOff).toEqual(zero);
    expect(restored.old).toBe(-1);
    expect(restored.resumed).toEqual(supported ? { ...zero, create: 8, begin: 1, end: 1 } : zero);
    expect(restored.timerExtensions).toBe(expected.gpu ? (supportedAfterRestore ? 3 : 2) : 0);
    const pixels = await page.evaluate(() => {
      const api = (window as unknown as { __nt: DiagnosticsHooks }).__nt;
      return [api.float_probe(0), api.float_probe(1)];
    });
    for (const [mode, pixel] of pixels.entries()) {
      if (pixel === -1) {
        test.info().annotations.push({ type: 'unsupported', description: `float probe ${mode} unavailable after restore; functional path unverified` });
        continue;
      }
      expect(pixel).toBeGreaterThanOrEqual(0);
      expect(Math.abs((pixel & 255) - 64)).toBeLessThanOrEqual(1);
      expect(Math.abs(((pixel >>> 8) & 255) - 128)).toBeLessThanOrEqual(1);
      expect(Math.abs(((pixel >>> 16) & 255) - 191)).toBeLessThanOrEqual(1);
    }
  }
});
