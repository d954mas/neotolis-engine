import { test, expect } from '@playwright/test';

type LinkControl = {
  hold: boolean;
  completionQueries: number;
  deletedPendingPrograms: number;
  extensionRequests: number;
  earlySyncCalls: number;
};
type LinkWindow = Window & {
  linkControl: LinkControl;
  __nt?: { ready: boolean; programs_ready(): boolean; drawn_frames(): number };
};

for (const parallel of [true, false]) {
  test('program linking: ' + (parallel ? 'simulated delayed KHR completion preserves owned programs' : 'missing extension still renders'), async ({ page }) => {
    test.setTimeout(60_000);
    const errors: string[] = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => {
      if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(message.text())) errors.push(message.text());
    });
    await page.addInitScript((parallel) => {
      const control: LinkControl = { hold: true, completionQueries: 0, deletedPendingPrograms: 0, extensionRequests: 0, earlySyncCalls: 0 };
      (window as LinkWindow).linkControl = control;
      const proto = WebGL2RenderingContext.prototype;
      const getExtension = proto.getExtension;
      let nativeParallel = false;
      proto.getExtension = function(name: string) {
        if (name === 'KHR_parallel_shader_compile') {
          control.extensionRequests++;
          if (!parallel) return null;
          const extension = getExtension.call(this, name);
          nativeParallel = extension !== null;
          // SwiftShader may omit KHR; emulate its completion result while linking real shaders.
          return extension ?? { COMPLETION_STATUS_KHR: 0x91b1 };
        }
        return getExtension.call(this, name);
      };
      // Polled but not yet reported complete: any other query on it would block the main thread.
      // Explicit waits never ask for completion, so their synchronous reads are not counted.
      const pending = new WeakSet<WebGLProgram>();
      const released = new WeakSet<WebGLProgram>();
      const getProgramParameter = proto.getProgramParameter;
      proto.getProgramParameter = function(program, name) {
        if (name === 0x91b1) { // COMPLETION_STATUS_KHR
          control.completionQueries++;
          if (!released.has(program)) pending.add(program);
          if (control.hold) return false;
          // A final pending result puts completion between two successive polls.
          if (!released.has(program)) {
            released.add(program);
            return false;
          }
          pending.delete(program);
          if (!nativeParallel) return getProgramParameter.call(this, program, this.LINK_STATUS);
        } else if (pending.has(program)) {
          control.earlySyncCalls++;
        }
        return getProgramParameter.call(this, program, name);
      };
      for (const name of ['getProgramInfoLog', 'getActiveUniform', 'getUniformLocation', 'getUniformBlockIndex', 'getAttachedShaders'] as const) {
        const original = proto[name] as (this: WebGL2RenderingContext, program: WebGLProgram, ...args: unknown[]) => unknown;
        (proto as unknown as Record<string, unknown>)[name] = function(this: WebGL2RenderingContext, program: WebGLProgram, ...args: unknown[]) {
          if (pending.has(program)) control.earlySyncCalls++;
          return original.call(this, program, ...args);
        };
      }
      const deleteProgram = proto.deleteProgram;
      proto.deleteProgram = function(program) {
        if (program && pending.has(program)) control.deletedPendingPrograms++;
        deleteProgram.call(this, program);
      };
    }, parallel);
    await page.goto('/index.html');

    if (parallel) {
      await page.waitForFunction(() => {
        const state = window as LinkWindow;
        return state.__nt !== undefined && state.linkControl.completionQueries >= 6;
      }, null, { timeout: 30_000 }).catch(async error => {
        const state = await page.evaluate(() => {
          const state = window as LinkWindow;
          return { control: state.linkControl, hooks: !!state.__nt, ready: state.__nt?.ready, drawn: state.__nt?.drawn_frames() };
        });
        throw new Error(`${error.message}; state=${JSON.stringify(state)}; errors=${JSON.stringify(errors)}`);
      });
      const pending = await page.evaluate(() => {
        const state = window as LinkWindow;
        const ready = state.__nt!.programs_ready();
        const before = state.linkControl.completionQueries;
        for (let i = 0; i < 20; i++) state.__nt!.programs_ready();
        return { ready, drawn: state.__nt!.drawn_frames(), extraQueries: state.linkControl.completionQueries - before, deleted: state.linkControl.deletedPendingPrograms };
      });
      expect(pending).toEqual({ ready: false, drawn: 0, extraQueries: 0, deleted: 0 });
      expect(await page.evaluate(() => (window as LinkWindow).linkControl.earlySyncCalls)).toBe(0);
      await page.evaluate(() => { (window as LinkWindow).linkControl.hold = false; });
    }

    await page.waitForFunction(() => {
      const hooks = (window as LinkWindow).__nt;
      return hooks?.ready && hooks.programs_ready() && hooks.drawn_frames() > 2;
    }, null, { timeout: 30_000 });
    const control = await page.evaluate(() => (window as LinkWindow).linkControl);
    expect(control.extensionRequests).toBeGreaterThan(0);
    expect(control.deletedPendingPrograms).toBe(0);
    expect(control.earlySyncCalls).toBe(0);
    if (!parallel) expect(control.completionQueries).toBe(0);
    expect(errors, 'unexpected browser/gfx errors').toEqual([]);
  });
}
