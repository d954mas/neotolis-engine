import { test, expect, type Page } from '@playwright/test';

const KEY = 'nt.test.crash';
const LOG_LEVEL = Number(process.env.NT_DIAGNOSTICS_LOG ?? 0);

type Identity = { vendor: string | null; renderer: string | null; api: string | null };
type DeviceHooks = {
  ready: boolean;
  programs_ready(): boolean;
  drawn_frames(): number;
  restore_frames(): number;
  observe_record(enabled: number): void;
  observe_value(index: number): number;
  crash_load(): string;
};
type DeviceWindow = Window & {
  __nt?: DeviceHooks;
  __device: Identity & { queries: number[]; extensions: string[]; denyCreate: boolean; denied: number };
  __deviceLoss?: WEBGL_lose_context;
};

const INITIAL: Identity = { vendor: 'vendor-initial', renderer: 'renderer-initial', api: 'WebGL 2.0 initial' };
const RESTORED: Identity = { vendor: 'vendor-restored', renderer: 'renderer-restored', api: 'WebGL 2.0 restored' };

// Override browser identity and context-creation responses; capture and persistence use the real engine.
async function installIdentity(page: Page, identity = INITIAL, debug = true, denyCreate = false): Promise<void> {
  await page.addInitScript(({ identity, debug, denyCreate }) => {
    const control = (window as DeviceWindow).__device = {
      ...identity, queries: [] as number[], extensions: [] as string[], denyCreate, denied: 0,
    };
    const proto = WebGL2RenderingContext.prototype;
    const getExtension = proto.getExtension;
    proto.getExtension = function(name: string) {
      control.extensions.push(name);
      if (name === 'WEBGL_debug_renderer_info') {
        return debug ? { UNMASKED_VENDOR_WEBGL: 0x9245, UNMASKED_RENDERER_WEBGL: 0x9246 } : null;
      }
      return getExtension.call(this, name);
    };
    const getParameter = proto.getParameter;
    proto.getParameter = function(pname: number) {
      if ([0x1f00, 0x1f01, 0x1f02, 0x9245, 0x9246].includes(pname)) control.queries.push(pname);
      if (pname === 0x9245) return control.vendor;
      if (pname === 0x9246) return control.renderer;
      if (pname === 0x1f00) return 'masked vendor';
      if (pname === 0x1f01) return 'masked renderer';
      if (pname === 0x1f02) return control.api;
      return getParameter.call(this, pname);
    };
    const getContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function(...args: Parameters<typeof getContext>) {
      if (control.denyCreate && args[0] === 'webgl2') {
        control.denied++;
        return null;
      }
      return getContext.apply(this, args);
    } as typeof getContext;
  }, { identity, debug, denyCreate });
}

async function ready(page: Page): Promise<void> {
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => {
    const hooks = (window as DeviceWindow).__nt;
    return hooks?.ready && hooks.programs_ready();
  }, null, { timeout: 30_000 });
}

async function capture(page: Page): Promise<string> {
  expect(await page.evaluate((key) => localStorage.getItem(key), KEY), 'no crash before the probe').toBeNull();
  const before = await page.evaluate(() => (window as DeviceWindow).__device.queries.length);
  await page.evaluate(() => {
    setTimeout(() => { throw new WebAssembly.RuntimeError('identity probe'); });
  });
  await page.waitForFunction((key) => localStorage.getItem(key) !== null, KEY, { timeout: 10_000 });
  expect(await page.evaluate(() => (window as DeviceWindow).__device.queries.length), 'capture must not query GL').toBe(before);
  const record = (await page.evaluate((key) => localStorage.getItem(key), KEY))!;
  expect(record).toContain('\nreason: RuntimeError: identity probe\n');
  return record;
}

function expectIdentity(record: string, identity: Identity): void {
  for (const [name, value] of [['gpu_vendor', identity.vendor], ['gpu_renderer', identity.renderer], ['gpu_api', identity.api]]) {
    if (value) expect(record.split('\n')).toContain(`${name}: ${value}`);
    else expect(record).not.toContain(`\n${name}:`);
  }
}

async function lose(page: Page): Promise<void> {
  const available = await page.evaluate(() => {
    const w = window as DeviceWindow;
    const gl = document.querySelector('canvas')!.getContext('webgl2')!;
    w.__deviceLoss = gl.getExtension('WEBGL_lose_context') ?? undefined;
    w.__deviceLoss?.loseContext();
    return !!w.__deviceLoss;
  });
  expect(available, 'WEBGL_lose_context is required for restore coverage').toBe(true);
  await page.waitForFunction(() => !(window as DeviceWindow).__nt!.programs_ready(), null, { timeout: 10_000 });
}

test('device identity: automatic init survives a new session without steady-frame queries', async ({ page, context }) => {
  test.setTimeout(90_000);
  await installIdentity(page);
  await ready(page);
  const baseline = await page.evaluate(() => ({
    queries: (window as DeviceWindow).__device.queries.slice(),
    frame: (window as DeviceWindow).__nt!.drawn_frames(),
    extensions: (window as DeviceWindow).__device.extensions.slice(),
  }));
  expect(baseline.extensions).toContain('WEBGL_debug_renderer_info');
  expect(baseline.queries).toEqual(expect.arrayContaining([0x9245, 0x9246, 0x1f02]));
  await page.waitForFunction((frame) => (window as DeviceWindow).__nt!.drawn_frames() >= frame + 6, baseline.frame);
  expect(await page.evaluate(() => (window as DeviceWindow).__device.queries)).toEqual(baseline.queries);
  const record = await capture(page);
  expectIdentity(record, INITIAL);

  const next = await context.newPage();
  await installIdentity(next, RESTORED);
  await ready(next);
  expect(await next.evaluate(() => (window as DeviceWindow).__nt!.crash_load())).toBe(record);
  await next.close();
});

test('device identity: successful restore refreshes every string', async ({ page }) => {
  test.setTimeout(90_000);
  await installIdentity(page);
  await ready(page);
  // The existing switch is inert in capture-OFF builds; the same restore runs in both.
  await page.evaluate(() => (window as DeviceWindow).__nt!.observe_record(1));
  await lose(page);
  const before = await page.evaluate((identity) => {
    const w = window as DeviceWindow;
    Object.assign(w.__device, identity);
    const count = w.__device.queries.length;
    w.__deviceLoss!.restoreContext();
    return count;
  }, RESTORED);
  await page.waitForFunction(() => (window as DeviceWindow).__nt!.programs_ready(), null, { timeout: 30_000 });
  const observation = await page.evaluate((before) => {
    const w = window as DeviceWindow;
    return {
      queries: w.__device.queries.slice(before),
      values: Array.from({ length: 7 }, (_, i) => w.__nt!.observe_value(31 + i)),
    };
  }, before);
  const expectedQueries = [0x9245, 0x9246, 0x1f02];
  expect(observation.queries).toEqual(expectedQueries);
  expect(observation.values[1], 'restore glGetString counter matches issued identity queries').toBe(3);
  if (observation.values[0]) {
    expect(observation.values[2], 'restore capture must not overflow').toBe(0);
    expect(observation.values[3], 'one BACKEND event per identity query').toBe(3);
    expect(observation.values.slice(4), 'captured query enums match actual WebGL calls').toEqual(expectedQueries);
  }
  expect(await page.evaluate(() => (window as DeviceWindow).__nt!.restore_frames())).toBe(1);
  await page.evaluate(() => (window as DeviceWindow).__nt!.observe_record(0));
  expectIdentity(await capture(page), RESTORED);
});

for (const failedRestore of [false, true]) {
  test(`device identity: ${failedRestore ? 'failed restore' : 'context loss'} retains the last observation`, async ({ page }) => {
    test.setTimeout(90_000);
    await installIdentity(page);
    await ready(page);
    await lose(page);
    if (failedRestore) {
      await page.evaluate(() => {
        const w = window as DeviceWindow;
        w.__device.denyCreate = true;
        w.__deviceLoss!.restoreContext();
      });
      await page.waitForFunction(() => (window as DeviceWindow).__device.denied > 0, null, { timeout: 30_000 });
      expect(await page.evaluate(() => (window as DeviceWindow).__nt!.programs_ready())).toBe(false);
    }
    const record = await capture(page);
    expectIdentity(record, INITIAL);
    if (failedRestore) {
      expect(record.match(/context-create: result=0/g) ?? []).toHaveLength(LOG_LEVEL < 3 ? 1 : 0);
      expect(record).not.toContain('WebGL context restore failed');
    }
  });
}

test('device identity: missing debug extension uses public WebGL identity', async ({ page }) => {
  await installIdentity(page, INITIAL, false);
  await ready(page);
  expectIdentity(await capture(page), { vendor: 'masked vendor', renderer: 'masked renderer', api: INITIAL.api });
  const queries = await page.evaluate(() => (window as DeviceWindow).__device.queries);
  expect(queries).not.toContain(0x9245);
  expect(queries).not.toContain(0x9246);
});

test('device identity: null fields, UTF-8 truncation and newlines stay bounded', async ({ page }) => {
  const renderer = 'GPU\r\n' + '😀'.repeat(160);
  await installIdentity(page, { vendor: null, renderer, api: 'WebGL 2.0\napi' });
  await ready(page);
  const record = await capture(page);
  expectIdentity(record, { vendor: null, renderer: 'GPU  ' + '😀'.repeat(126), api: 'WebGL 2.0 api' });
  expect(record).not.toContain('\uFFFD');
  expect(record).not.toContain('gpu_vendor: null');
  expect(Buffer.byteLength(record.split('\ngpu_renderer: ')[1].split('\n')[0], 'utf8')).toBe(509);
});

test('device identity: startup context failure is saved before app hooks exist', async ({ page }) => {
  await installIdentity(page, INITIAL, true, true);
  await page.goto('/index.html?crash=read');
  await page.waitForFunction((key) => localStorage.getItem(key) !== null, KEY, { timeout: 30_000 });
  const record = (await page.evaluate((key) => localStorage.getItem(key), KEY))!;
  expectIdentity(record, { vendor: null, renderer: null, api: null });
  expect(record).toMatch(/\nreason: /);
  expect(await page.evaluate(() => (window as DeviceWindow).__nt)).toBeUndefined();
  const log = record.slice(record.indexOf('\nlog:\n') + 6);
  if (LOG_LEVEL < 3) {
    expect(log).toMatch(/ERROR.*context-create/);
    expect(log).toMatch(/result=0/);
  } else {
    expect(log).toBe('');
  }
});
