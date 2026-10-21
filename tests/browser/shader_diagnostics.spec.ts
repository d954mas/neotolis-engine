import { test, expect } from '@playwright/test';

type Mode = 'vertex' | 'fragment' | 'both' | 'link' | 'long-log' | 'no-translation' | 'long-translation' | 'unavailable' | 'lost' | 'success';
type ProbeWindow = Window & {
  __nt?: { ready: boolean; programs_ready(): boolean; loss_window(step: number): number };
  __stages: { armed: boolean; queries: number; deletedQueries: number; programLogs: number; translations: number; failed: boolean };
};

for (const mode of ['vertex', 'fragment', 'both', 'link', 'long-log', 'no-translation', 'long-translation', 'unavailable', 'lost', 'success'] as Mode[]) {
  test(`shader diagnostics: ${mode}`, async ({ page }) => {
    await page.addInitScript((mode: Mode) => {
      const state = (window as ProbeWindow).__stages = { armed: !['unavailable', 'lost', 'success'].includes(mode), queries: 0, deletedQueries: 0, programLogs: 0, translations: 0, failed: false };
      const proto = WebGL2RenderingContext.prototype;
      const source = proto.shaderSource;
      const parameter = proto.getShaderParameter;
      const programParameter = proto.getProgramParameter;
      const link = proto.linkProgram;
      const remove = proto.deleteShader;
      const lost = proto.isContextLost;
      const extension = proto.getExtension;
      const deleted = new Set<WebGLShader>();
      // Debug Emscripten builds read each parameter twice, so count stages, not calls.
      const queried = new Set<WebGLShader>();
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
          if (!queried.has(shader)) state.queries++;
          queried.add(shader);
          if (deleted.has(shader)) state.deletedQueries++;
        }
        return parameter.call(this, shader, query);
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
      proto.getExtension = function(name: string) {
        const result = extension.call(this, name);
        if (name !== 'WEBGL_debug_shaders') return result;
        if (mode === 'no-translation' || !result) return null;
        return {
          getTranslatedShaderSource: (shader: WebGLShader) => {
            state.translations++;
            return mode === 'long-translation' ? ('translated line ' + 'y'.repeat(40) + '\n').repeat(200) : result.getTranslatedShaderSource(shader);
          },
        };
      } as typeof proto.getExtension;
    }, mode);
    await page.goto('/index.html?crash=read');
    if (mode === 'success' || mode === 'unavailable' || mode === 'lost') {
      await page.waitForFunction(() => {
        const hooks = (window as ProbeWindow).__nt;
        return hooks?.ready && hooks.programs_ready();
      });
      expect(await page.evaluate(() => (window as ProbeWindow).__stages.queries)).toBe(0);
      if (mode === 'success') {
        expect(await page.evaluate(() => (window as ProbeWindow).__stages.translations)).toBe(0);
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
        expect(state.translations).toBe(0);
        expect(await page.evaluate(() => localStorage.getItem('nt.test.crash'))).toBeNull();
        return;
      }
    }
    await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
    const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
    const lines = record.split('\n');
    const at = lines.findIndex(line => line.includes(' link: link failed:'));
    expect(lines[at]).toMatch(/program \d+\/GL \d+ link: link failed: gl_error=0x0 "[^"\n]+"$/);
    const vertex = mode === 'unavailable' ? -1 : mode === 'vertex' || mode === 'both' ? 0 : 1;
    const fragment = mode === 'unavailable' ? -1 : mode === 'fragment' || mode === 'both' ? 0 : 1;
    expect(lines[at - 1]).toContain(`program link stages: vs_compile=${vertex} fs_compile=${fragment}`);
    const state = await page.evaluate(() => (window as ProbeWindow).__stages);
    expect(state.queries).toBe(mode === 'unavailable' ? 0 : 2);
    expect(state.deletedQueries).toBe(0);
    // A stage ANGLE rejected has no translation; a link failure translates both live stages.
    const translated = (stage: string) => lines.findIndex(line => line.includes(`${stage} translated:`));
    const expected = mode === 'unavailable' ? [] : mode === 'vertex' ? ['vertex'] : mode === 'fragment' ? ['fragment'] : ['vertex', 'fragment'];
    for (const stage of ['vertex', 'fragment']) {
      const header = translated(stage);
      expect(header >= 0).toBe(expected.includes(stage));
      if (header < 0) continue;
      expect(header).toBeLessThan(at - 1);
      const unavailable = mode === 'no-translation' || mode === 'vertex' || mode === 'fragment' || mode === 'both';
      expect(lines[header].endsWith(`${stage} translated: unavailable`)).toBe(unavailable);
      if (!unavailable) expect(lines[header + 1]).not.toContain(' link stages:');
    }
    expect(state.translations).toBe(mode === 'no-translation' || mode === 'unavailable' ? 0 : expected.length);
    if (mode === 'long-translation') {
      expect(record).toMatch(/vertex translated: truncated at 2047 of 11400 bytes/);
      expect(record).toMatch(/fragment translated: truncated at 2047 of 11400 bytes/);
    }
    if (mode === 'long-log') {
      expect(record).toContain('driver detail');
      expect(record.lastIndexOf('program link stages:')).toBeGreaterThan(record.lastIndexOf('driver detail'));
    }
  });
}
