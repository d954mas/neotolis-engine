import { test, expect, type Page } from '@playwright/test';

const KEY = 'nt.test.crash';

type Identity = { vendor: string | null; renderer: string | null; api: string | null };
type DeviceHooks = {
  ready: boolean;
  programs_ready(): boolean;
  drawn_frames(): number;
  restore_frames(): number;
  observe_value(index: number): number;
  crash_load(): string;
};
type DeviceWindow = Window & {
  __nt?: DeviceHooks;
  __device: Identity & { queries: number[]; extensions: string[]; deny: string[] | null; denied: number; creationErrorListeners: Set<EventListenerOrEventListenerObject> };
  __deviceLoss?: WEBGL_lose_context;
};

const INITIAL: Identity = { vendor: 'vendor-initial', renderer: 'renderer-initial', api: 'WebGL 2.0 initial' };
const RESTORED: Identity = { vendor: 'vendor-restored', renderer: 'renderer-restored', api: 'WebGL 2.0 restored' };

// Override browser identity and context-creation responses; capture and persistence use the real engine.
// deny: null creates normally; each string otherwise becomes one creation-error status before refusal.
async function installIdentity(page: Page, identity = INITIAL, debug = true, deny: string[] | null = null, webgl2Api = true): Promise<void> {
  await page.addInitScript(({ identity, debug, deny, webgl2Api }) => {
    const control = (window as DeviceWindow).__device = {
      ...identity, queries: [] as number[], extensions: [] as string[], deny, denied: 0, creationErrorListeners: new Set<EventListenerOrEventListenerObject>(),
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
      if (control.deny !== null && args[0] === 'webgl2') {
        control.denied++;
        for (const statusMessage of control.deny) this.dispatchEvent(new WebGLContextEvent('webglcontextcreationerror', { statusMessage }));
        return null;
      }
      return getContext.apply(this, args);
    } as typeof getContext;
    const addEventListener = HTMLCanvasElement.prototype.addEventListener;
    const removeEventListener = HTMLCanvasElement.prototype.removeEventListener;
    HTMLCanvasElement.prototype.addEventListener = function(...args: Parameters<typeof addEventListener>) {
      if (args[0] === 'webglcontextcreationerror') control.creationErrorListeners.add(args[1]);
      return addEventListener.apply(this, args);
    } as typeof addEventListener;
    HTMLCanvasElement.prototype.removeEventListener = function(...args: Parameters<typeof removeEventListener>) {
      if (args[0] === 'webglcontextcreationerror') control.creationErrorListeners.delete(args[1]);
      return removeEventListener.apply(this, args);
    } as typeof removeEventListener;
    if (!webgl2Api) Object.defineProperty(window, 'WebGL2RenderingContext', { configurable: true, value: undefined });
  }, { identity, debug, deny, webgl2Api });
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
  expect(record).toMatch(/\ngpu_caps: astc=[01] bc7=[01] etc2=[01] float_rt=[01] float_linear=[01] parallel_compile=[01] max_texture=[1-9]\d* ubo_align=[1-9]\d*\n/);

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
      counter: w.__nt!.observe_value(31),
    };
  }, before);
  expect(observation.queries).toEqual([0x9245, 0x9246, 0x1f02]);
  expect(observation.counter, 'restore glGetString counter matches issued identity queries').toBe(3);
  expect(await page.evaluate(() => (window as DeviceWindow).__nt!.restore_frames())).toBe(1);
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
        w.__device.deny = [];
        w.__deviceLoss!.restoreContext();
      });
      await page.waitForFunction(() => (window as DeviceWindow).__device.denied > 0, null, { timeout: 30_000 });
      expect(await page.evaluate(() => (window as DeviceWindow).__nt!.programs_ready())).toBe(false);
    }
    const record = await capture(page);
    expectIdentity(record, INITIAL);
    if (failedRestore) {
      expect(record.match(/context-create: failed .* browser_status=<empty>\n/g) ?? []).toHaveLength(1);
      expect(record.match(/context restore failed/g) ?? []).toHaveLength(1);
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
  const renderer = 'GPU\r\n' + '😀'.repeat(80);
  await installIdentity(page, { vendor: null, renderer, api: 'WebGL 2.0\napi' });
  await ready(page);
  const record = await capture(page);
  expectIdentity(record, { vendor: null, renderer: 'GPU  ' + '😀'.repeat(62), api: 'WebGL 2.0 api' });
  expect(record).not.toContain('\uFFFD');
  expect(Buffer.byteLength(record.split('\ngpu_renderer: ')[1].split('\n')[0], 'utf8')).toBe(253);
});

for (const { name, statuses, webgl2Api, expected } of [
  {
    name: 'the ordered browser sequence',
    statuses: ['Could not create a WebGL context: GPU provider creation failed', 'Web page caused context loss and was blocked'],
    webgl2Api: true,
    expected: 'webgl2_api=1 browser_status=Could not create a WebGL context: GPU provider creation failed | Web page caused context loss and was blocked',
  },
  { name: 'no browser event', statuses: [], webgl2Api: true, expected: 'webgl2_api=1 browser_status=<empty>' },
  { name: 'empty browser events', statuses: ['', ''], webgl2Api: true, expected: 'webgl2_api=1 browser_status=<empty>' },
  { name: 'a missing WebGL2 API', statuses: [], webgl2Api: false, expected: 'webgl2_api=0 browser_status=<empty>' },
] as const) {
test(`device identity: startup context failure preserves ${name}`, async ({ page }) => {
  await installIdentity(page, INITIAL, true, [...statuses], webgl2Api);
  await page.goto('/index.html?crash=read');
  await page.waitForFunction((key) => localStorage.getItem(key) !== null, KEY, { timeout: 30_000 });
  const record = (await page.evaluate((key) => localStorage.getItem(key), KEY))!;
  expectIdentity(record, { vendor: null, renderer: null, api: null });
  expect(record).not.toContain('\ngpu_caps:');
  expect(record).toMatch(/\nreason: /);
  expect(await page.evaluate(() => (window as DeviceWindow).__nt)).toBeUndefined();
  const line = record.slice(record.indexOf('\nlog:\n') + 6).match(/context-create: failed .*/)?.[0] ?? '';
  expect(line).toContain(expected);
  expect(Buffer.byteLength(line, 'utf8')).toBeLessThan(512);
  expect(await page.evaluate(() => (window as DeviceWindow).__device.creationErrorListeners.size)).toBe(0);
});
}

test('device identity: successful creation removes the status listener', async ({ page }) => {
  await installIdentity(page);
  await ready(page);
  expect(await page.evaluate(() => (window as DeviceWindow).__device.creationErrorListeners.size)).toBe(0);
});

test('device identity: browser status text preserves the next status after a cut', async ({ page }) => {
  await installIdentity(page, INITIAL, true, [`reason\r\n\0${'ы'.repeat(300)}`, 'blocked', 'ignored']);
  await page.goto('/index.html?crash=read');
  await page.waitForFunction((key) => localStorage.getItem(key) !== null, KEY, { timeout: 30_000 });
  const record = (await page.evaluate((key) => localStorage.getItem(key), KEY))!;
  const line = record.slice(record.indexOf('\nlog:\n') + 6).match(/context-create: failed .*/)?.[0] ?? '';
  expect(line).toMatch(/browser_status=reason   ы+… \| blocked$/);
  expect(line).not.toContain('ignored');
  expect(line).not.toContain('\uFFFD');
  expect(Buffer.byteLength(line, 'utf8')).toBeLessThan(512);
});

test('device identity: browser status cuts surrogate pairs at a code point boundary', async ({ page }) => {
  await installIdentity(page, INITIAL, true, [`${'x'.repeat(95)}😀`, `${'y'.repeat(95)}😀`]);
  await page.goto('/index.html?crash=read');
  await page.waitForFunction((key) => localStorage.getItem(key) !== null, KEY, { timeout: 30_000 });
  const record = (await page.evaluate((key) => localStorage.getItem(key), KEY))!;
  const line = record.slice(record.indexOf('\nlog:\n') + 6).match(/context-create: failed .*/)?.[0] ?? '';
  expect(line).toMatch(new RegExp(`browser_status=${'x'.repeat(95)}… \\| ${'y'.repeat(95)}…$`));
  expect(line).not.toContain('\uFFFD');
});
