import { test, expect, type Page } from '@playwright/test';

// Crash record (engine/crash): a crashing visit stores the record and dispatches 'nt-crash';
// the next visit reads and clears it through nt_crash_load_previous / nt_crash_clear_previous.
const KEY = 'nt.test.crash';

type CrashWindow = Window & {
  __nt?: { crash_load(): string; crash_clear(): void };
  __crashEvents?: string[];
};

async function openRead(page: Page): Promise<void> {
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => (window as CrashWindow).__nt !== undefined, null, { timeout: 30_000 });
}

for (const mode of ['trap', 'abort'] as const) {
  test(`crash record: ${mode} is stored, announced, read and cleared`, async ({ page }) => {
    test.setTimeout(120_000);
    await page.addInitScript(() => {
      const w = window as CrashWindow;
      w.__crashEvents = [];
      window.addEventListener('nt-crash', (e) => w.__crashEvents!.push((e as CustomEvent).detail.text));
    });

    // A clean slate (a CI retry would otherwise read the first attempt's record), then a foreign
    // JS error without wasm frames must not produce a record.
    await openRead(page);
    await page.evaluate((key) => {
      localStorage.removeItem(key);
      setTimeout(() => { throw new Error('foreign error'); });
    }, KEY);
    await page.waitForTimeout(200);
    expect(await page.evaluate((key) => localStorage.getItem(key), KEY)).toBeNull();

    const crashed = page.waitForEvent('pageerror', { timeout: 60_000 });
    await page.goto(`/index.html?crash=${mode}`);
    expect((await crashed).message).toMatch(mode === 'trap' ? /unreachable/ : /Abort/);
    await page.waitForFunction(() => ((window as CrashWindow).__crashEvents ?? []).length > 0, null, { timeout: 10_000 });

    // After the crash only JS runs: the instance is dead.
    const stored = await page.evaluate((key) => localStorage.getItem(key), KEY);
    expect(stored).not.toBeNull();
    expect(await page.evaluate(() => (window as CrashWindow).__crashEvents)).toEqual([stored]);

    // One record per session: a later error does not overwrite it.
    await page.evaluate(() => { setTimeout(() => { throw new WebAssembly.RuntimeError('second'); }); });
    await page.waitForTimeout(200);
    expect(await page.evaluate((key) => localStorage.getItem(key), KEY)).toBe(stored);

    await openRead(page);
    const record = await page.evaluate(() => (window as CrashWindow).__nt!.crash_load());
    expect(record).toBe(stored);
    expect(record).toMatch(/^game: browser_smoke-test\nengine: \d+\.\d+\.\d+\+([0-9a-f]{8}(-dirty)?|unknown) \S+\ntime: \d+\nreason: /);
    expect(record).toMatch(mode === 'trap' ? /\nreason: RuntimeError: unreachable/ : /\nreason: RuntimeError: Abort/);
    expect(record).toMatch(/\nstack:\n[^]*wasm/);

    // The ring wrapped (300 lines > 8 KB): the oldest lines are gone, the first kept line is whole,
    // the token line written right before the crash is last.
    const log = record.slice(record.indexOf('\nlog:\n') + 6).split('\n').filter((line) => line !== '');
    expect(log[0]).toMatch(/^WARN crash probe line \d+: проверка кольца$/);
    expect(log).not.toContain('WARN crash probe line 0: проверка кольца');
    expect(log[log.length - 1]).toBe(`ERROR crash probe token-${mode}`);

    await page.evaluate(() => (window as CrashWindow).__nt!.crash_clear());
    expect(await page.evaluate(() => (window as CrashWindow).__nt!.crash_load())).toBe('');
  });
}
