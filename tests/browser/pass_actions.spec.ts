import { test, expect } from '@playwright/test';

type Hooks = { ready: boolean; programs_ready(): boolean; pass_actions_probe(mode: number): number; restore_frames(): number };
type Probe = { active: boolean; calls: { name: string; args: number[]; fbo: boolean }[]; errors: number[] };

test('pass actions preserve shared depth, clear fully, clear under scissor and invalidate before unbind across restore', async ({ page }) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const probe: Probe = { active: false, calls: [], errors: [] };
    (window as unknown as { passProbe: Probe }).passProbe = probe;
    const proto = WebGL2RenderingContext.prototype;
    for (const name of ['invalidateFramebuffer', 'clear', 'drawArrays', 'bindFramebuffer'] as const) {
      const original = proto[name] as (...args: unknown[]) => unknown;
      (proto as unknown as Record<string, unknown>)[name] = function (this: WebGL2RenderingContext, ...args: unknown[]) {
        const result = original.apply(this, args);
        if (probe.active) {
          probe.calls.push({ name, args: name === 'invalidateFramebuffer' ? Array.from(args[1] as number[]) : [args[0] as number],
            fbo: this.getParameter(this.DRAW_FRAMEBUFFER_BINDING) !== null });
          // Check before engine readback can drain an earlier GL error.
          const error = this.getError();
          if (error !== this.NO_ERROR) probe.errors.push(error);
        }
        return result;
      };
    }
  });
  await page.goto('/');
  await page.waitForFunction(() => {
    const hooks = (window as unknown as { __nt?: Hooks }).__nt;
    return hooks?.ready && hooks.programs_ready();
  });
  for (let cycle = 0; cycle < 2; cycle++) {
    const runs = await page.evaluate(() => {
      const hooks = (window as unknown as { __nt: Hooks }).__nt;
      const probe = (window as unknown as { passProbe: Probe }).passProbe;
      return [0, 1].map(mode => {
        probe.calls = [];
        probe.errors = [];
        probe.active = true;
        const result = hooks.pass_actions_probe(mode);
        probe.active = false;
        return { result, calls: probe.calls, errors: probe.errors };
      });
    });
    for (const run of runs) {
      expect(run.result).toBe(247);
      expect(run.errors).toEqual([]);
      const invalidates = run.calls.filter(call => call.name === 'invalidateFramebuffer');
      expect(invalidates).toEqual([
        { name: 'invalidateFramebuffer', args: [0x8d00, 0x8d20], fbo: true },
        { name: 'invalidateFramebuffer', args: [0x1801, 0x1802], fbo: false },
      ]);
      expect(run.calls.filter(call => call.name === 'clear').map(call => call.args[0])).toEqual([0x4100, 0x100, 0x4000, 0x4000, 0x100, 0x4000]);
      expect(run.calls.filter(call => call.name === 'drawArrays')).toHaveLength(6);
      const firstDiscard = run.calls.findIndex(call => call.name === 'invalidateFramebuffer');
      expect(run.calls.slice(firstDiscard - 1, firstDiscard + 2).map(call => call.name)).toEqual([
        'drawArrays', 'invalidateFramebuffer', 'bindFramebuffer',
      ]);
    }
    if (cycle === 0) {
      await page.evaluate(() => {
        const gl = document.querySelector('canvas')!.getContext('webgl2')!;
        const extension = gl.getExtension('WEBGL_lose_context')!;
        extension.loseContext();
        setTimeout(() => extension.restoreContext(), 100);
      });
      await page.waitForFunction(() => {
        const hooks = (window as unknown as { __nt: Hooks }).__nt;
        return hooks.restore_frames() > 0 && hooks.programs_ready();
      });
    }
  }
  expect(errors).toEqual([]);
});
