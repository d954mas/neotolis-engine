import { test, expect } from '@playwright/test';

type LinkControl = {
  hold: boolean;
  completionQueries: number;
  earlySyncCalls: number;
  extensionRequests: number;
};
type LinkWindow = Window & {
  linkControl: LinkControl;
  __nt?: { ready: boolean; programs_ready(): boolean; drawn_frames(): number };
};

// Timing checks need a release build: Emscripten GL_DEBUG reads the program log right after linkProgram.
for (const parallel of [true, false]) {
  test('program linking: ' + (parallel ? 'held KHR completion never blocks and the scene appears after release' : 'without the extension every link finishes and the scene renders'), async ({ page }) => {
    test.setTimeout(60_000);
    const errors: string[] = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => {
      if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(message.text())) errors.push(message.text());
    });
    await page.addInitScript((parallel) => {
      const control: LinkControl = { hold: parallel, completionQueries: 0, earlySyncCalls: 0, extensionRequests: 0 };
      (window as LinkWindow).linkControl = control;
      const proto = WebGL2RenderingContext.prototype;
      const getExtension = proto.getExtension;
      proto.getExtension = function(name: string) {
        if (name === 'KHR_parallel_shader_compile') {
          control.extensionRequests++;
          if (!parallel) return null;
          // SwiftShader may omit KHR; the constant is all the engine needs from the object.
          return getExtension.call(this, name) ?? { COMPLETION_STATUS_KHR: 0x91b1 };
        }
        return getExtension.call(this, name);
      };
      // A program is pending from linkProgram until a completion query reports it done; any other
      // query on it in between would block the main thread until the driver finishes the link.
      const pending = new WeakSet<WebGLProgram>();
      const linkProgram = proto.linkProgram;
      proto.linkProgram = function(program) {
        if (parallel) pending.add(program);
        linkProgram.call(this, program);
      };
      const getProgramParameter = proto.getProgramParameter;
      proto.getProgramParameter = function(program, name) {
        if (name === 0x91b1) { // COMPLETION_STATUS_KHR
          control.completionQueries++;
          if (control.hold) return false;
          pending.delete(program);
          return true;
        }
        if (pending.has(program)) control.earlySyncCalls++;
        return getProgramParameter.call(this, program, name);
      };
      for (const name of ['getProgramInfoLog', 'getActiveUniform', 'getUniformLocation', 'getUniformBlockIndex', 'getAttachedShaders', 'useProgram'] as const) {
        const original = proto[name] as (this: WebGL2RenderingContext, program: WebGLProgram, ...args: unknown[]) => unknown;
        (proto as unknown as Record<string, unknown>)[name] = function(this: WebGL2RenderingContext, program: WebGLProgram, ...args: unknown[]) {
          if (program && pending.has(program)) control.earlySyncCalls++;
          return original.call(this, program, ...args);
        };
      }
    }, parallel);
    await page.goto('/index.html');

    if (parallel) {
      // Frames keep running while every link is held: each begin_frame polls each pending program once.
      await page.waitForFunction(() => (window as LinkWindow).linkControl.completionQueries >= 20, null, { timeout: 30_000 });
      const held = await page.evaluate(async () => {
        const state = window as LinkWindow;
        const before = state.linkControl.completionQueries;
        const reads = Array.from({ length: 20 }, () => state.__nt!.programs_ready());
        const polledByReads = state.linkControl.completionQueries - before;
        await new Promise(resolve => setTimeout(resolve, 300));
        return { ready: reads.some(Boolean), polledByReads, advanced: state.linkControl.completionQueries > before };
      });
      expect(held).toEqual({ ready: false, polledByReads: 0, advanced: true });
      expect(await page.evaluate(() => (window as LinkWindow).linkControl.earlySyncCalls)).toBe(0);
      await page.evaluate(() => { (window as LinkWindow).linkControl.hold = false; });
    }

    await page.waitForFunction(() => {
      const hooks = (window as LinkWindow).__nt;
      return hooks?.ready && hooks.programs_ready() && hooks.drawn_frames() > 2;
    }, null, { timeout: 30_000 });
    const control = await page.evaluate(() => (window as LinkWindow).linkControl);
    expect(control.extensionRequests).toBeGreaterThan(0);
    expect(control.earlySyncCalls).toBe(0);
    if (!parallel) expect(control.completionQueries).toBe(0);
    expect(errors, 'unexpected browser/gfx errors').toEqual([]);
  });
}
