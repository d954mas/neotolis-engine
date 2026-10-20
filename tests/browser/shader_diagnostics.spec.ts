import { test, expect } from '@playwright/test';

type Mode = 'vertex' | 'fragment' | 'both' | 'link' | 'long-log' | 'unavailable' | 'null-status' | 'lost' | 'success';
type ProbeWindow = Window & {
  __nt?: { ready: boolean; programs_ready(): boolean; loss_window(step: number): number };
  __stages: { armed: boolean; queries: number; deletedQueries: number; programLogs: number; failed: boolean };
};

for (const mode of ['vertex', 'fragment', 'both', 'link', 'long-log', 'unavailable', 'null-status', 'lost', 'success'] as Mode[]) {
  test(`shader diagnostics: ${mode}`, async ({ page }) => {
    await page.addInitScript((mode: Mode) => {
      const state = (window as ProbeWindow).__stages = { armed: !['unavailable', 'lost', 'success'].includes(mode), queries: 0, deletedQueries: 0, programLogs: 0, failed: false };
      const proto = WebGL2RenderingContext.prototype;
      const source = proto.shaderSource;
      const parameter = proto.getShaderParameter;
      const programParameter = proto.getProgramParameter;
      const link = proto.linkProgram;
      const attached = proto.getAttachedShaders;
      const remove = proto.deleteShader;
      const lost = proto.isContextLost;
      const deleted = new Set<WebGLShader>();
      let target: WebGLProgram | null = null;
      proto.shaderSource = function(shader, text) {
        const type = parameter.call(this, shader, this.SHADER_TYPE);
        const broken = state.armed && (mode === 'both' || mode === 'vertex' && type === this.VERTEX_SHADER || mode === 'fragment' && type === this.FRAGMENT_SHADER);
        return source.call(this, shader, broken ? '#version 300 es\ninvalid shader source' : text);
      };
      proto.linkProgram = function(program) {
        if (state.armed && !target) target = program;
        return link.call(this, program);
      };
      proto.getProgramParameter = function(program, query) {
        if (program === target && query === this.LINK_STATUS) {
          state.failed = true;
          return false;
        }
        return programParameter.call(this, program, query);
      };
      proto.getShaderParameter = function(shader, query) {
        if (query === this.COMPILE_STATUS) {
          state.queries++;
          if (deleted.has(shader)) state.deletedQueries++;
          if (mode === 'null-status' && parameter.call(this, shader, this.SHADER_TYPE) === this.VERTEX_SHADER) return null;
        }
        return parameter.call(this, shader, query);
      };
      proto.getAttachedShaders = function(program) {
        return attached.call(this, program)?.reverse() ?? null;
      };
      proto.deleteShader = function(shader) {
        if (shader) deleted.add(shader);
        return remove.call(this, shader);
      };
      proto.isContextLost = function() { return mode === 'lost' && state.failed || lost.call(this); };
      proto.getProgramInfoLog = function(program) {
        if (state.failed) state.programLogs++;
        return program === target && mode === 'long-log' ? ('driver detail ' + 'x'.repeat(80) + '\n').repeat(256) : '';
      };
      proto.getShaderInfoLog = function() { return ''; };
    }, mode);
    await page.goto('/index.html?crash=read');
    if (mode === 'success' || mode === 'unavailable' || mode === 'lost') {
      await page.waitForFunction(() => {
        const hooks = (window as ProbeWindow).__nt;
        return hooks?.ready && hooks.programs_ready();
      });
      expect(await page.evaluate(() => (window as ProbeWindow).__stages.queries)).toBe(0);
      if (mode === 'success') {
        expect(await page.evaluate(() => localStorage.getItem('nt.test.crash'))).toBeNull();
        return;
      }
      await page.evaluate(() => {
        const w = window as ProbeWindow;
        w.__stages.armed = true;
        w.__nt!.loss_window(0);
        w.__nt!.loss_window(1);
      });
      if (mode === 'lost') {
        await page.waitForFunction(() => (window as ProbeWindow).__stages.failed);
        const state = await page.evaluate(() => (window as ProbeWindow).__stages);
        expect(state.queries).toBe(0);
        expect(state.programLogs).toBe(0);
        expect(await page.evaluate(() => localStorage.getItem('nt.test.crash'))).toBeNull();
        return;
      }
    }
    await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
    const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
    expect(record.includes('shader not bound to local id')).toBe(false);
    const summary = record.split('\n').find(line => line.includes(' link: link failed:'))!;
    expect(summary).toMatch(/program \d+\/GL \d+ link: link failed: gl_error=0x0 /);
    const vertex = mode === 'unavailable' || mode === 'null-status' ? -1 : mode === 'vertex' || mode === 'both' ? 0 : 1;
    const fragment = mode === 'unavailable' ? -1 : mode === 'fragment' || mode === 'both' ? 0 : 1;
    expect(summary).toContain(`vs_compile=${vertex} fs_compile=${fragment}`);
    expect(summary).toMatch(/"[^"\n]+"$/);
    const state = await page.evaluate(() => (window as ProbeWindow).__stages);
    expect(state.queries).toBe(mode === 'unavailable' ? 0 : 2);
    expect(state.deletedQueries).toBe(0);
    if (mode === 'long-log') {
      expect(record).toContain('driver detail');
      expect(record.lastIndexOf(' link: link failed:')).toBeGreaterThan(record.lastIndexOf('driver detail'));
    }
  });
}
