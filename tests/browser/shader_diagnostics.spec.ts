import { test, expect } from '@playwright/test';

type Mode = 'vertex' | 'fragment' | 'link' | 'no-translation' | 'long-translation' | 'destroyed' | 'lost';
type Probe = { armed: boolean; queries: number; programLogs: number; translations: number; enables: number; failed: boolean };
type ProbeWindow = Window & {
  __nt?: { ready: boolean; programs_ready(): boolean; loss_window(step: number): number };
  __stages: Probe;
};

for (const mode of ['vertex', 'fragment', 'link', 'no-translation', 'long-translation', 'destroyed', 'lost'] as Mode[]) {
  test(`shader diagnostics: ${mode}`, async ({ page }) => {
    await page.addInitScript((mode: Mode) => {
      const state = (window as ProbeWindow).__stages = { armed: mode !== 'destroyed', queries: 0, programLogs: 0, translations: 0, enables: 0, failed: false };
      const proto = WebGL2RenderingContext.prototype;
      const source = proto.shaderSource;
      const parameter = proto.getShaderParameter;
      const programParameter = proto.getProgramParameter;
      const link = proto.linkProgram;
      const lost = proto.isContextLost;
      const extension = proto.getExtension;
      // Debug Emscripten builds read each parameter twice, so count stages, not calls.
      const queried = new Set<WebGLShader>();
      let target: WebGLProgram | null = null;
      proto.shaderSource = function(shader, text) {
        const type = parameter.call(this, shader, this.SHADER_TYPE);
        const broken = state.armed && (mode === 'vertex' && type === this.VERTEX_SHADER || mode === 'fragment' && type === this.FRAGMENT_SHADER);
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
        if (query === this.COMPILE_STATUS && !queried.has(shader)) {
          state.queries++;
          queried.add(shader);
        }
        return parameter.call(this, shader, query);
      };
      proto.isContextLost = function() { return mode === 'lost' && state.failed || lost.call(this); };
      proto.getProgramInfoLog = function(program) {
        if (state.failed) state.programLogs++;
        return program === target && mode === 'link' ? ('driver detail ' + 'x'.repeat(80) + '\n').repeat(20) : '';
      };
      proto.getShaderInfoLog = function() { return ''; };
      proto.getExtension = function(this: WebGL2RenderingContext, name: string) {
        const result = extension.call(this, name);
        if (name !== 'WEBGL_debug_shaders') return result;
        state.enables++;
        if (mode === 'no-translation' || !result) return null;
        return {
          getTranslatedShaderSource: (shader: WebGLShader) => {
            state.translations++;
            if (mode === 'long-translation') return ('translated line ' + 'y'.repeat(40) + '\n').repeat(200);
            const text = result.getTranslatedShaderSource(shader);
            // Tag the stage so the record proves whose text it holds.
            const stage = parameter.call(this, shader, this.SHADER_TYPE) === this.VERTEX_SHADER ? 'vertex' : 'fragment';
            return text ? `// ${stage} stage\n${text}` : text;
          },
        };
      } as typeof proto.getExtension;
    }, mode);
    await page.goto('/index.html?crash=read');
    const probe = () => page.evaluate(() => (window as ProbeWindow).__stages);
    const crash = () => page.evaluate(() => localStorage.getItem('nt.test.crash'));
    if (mode === 'destroyed') {
      await page.waitForFunction(() => {
        const hooks = (window as ProbeWindow).__nt;
        return hooks?.ready && hooks.programs_ready();
      });
      // Successful links query no stage and never enable the extension.
      expect(await probe()).toMatchObject({ queries: 0, translations: 0, enables: 0 });
      expect(await crash()).toBeNull();
      await page.evaluate(() => {
        const w = window as ProbeWindow;
        w.__stages.armed = true;
        w.__nt!.loss_window(0);
        w.__nt!.loss_window(1);
      });
    }
    if (mode === 'lost') {
      await page.waitForFunction(() => (window as ProbeWindow).__stages.failed);
      expect(await probe()).toMatchObject({ queries: 0, programLogs: 0, translations: 0, enables: 0 });
      expect(await crash()).toBeNull();
      return;
    }
    await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
    const record = (await crash())!;
    const lines = record.split('\n');
    const at = lines.findIndex(line => line.includes(' link: link failed:'));
    expect(lines[at]).toMatch(/program \d+\/GL \d+ link: link failed: gl_error=0x0 "[^"\n]+"$/);
    const vertex = mode === 'destroyed' ? -1 : mode === 'vertex' ? 0 : 1;
    const fragment = mode === 'destroyed' ? -1 : mode === 'fragment' ? 0 : 1;
    expect(lines[at - 1]).toContain(`program link stages: vs_compile=${vertex} fs_compile=${fragment}`);

    // A compile failure translates only the failed stage; a link failure both live stages.
    const stages = mode === 'destroyed' ? [] : mode === 'vertex' ? ['vertex'] : mode === 'fragment' ? ['fragment'] : ['vertex', 'fragment'];
    const driver = lines.findIndex(line => line.includes('driver detail'));
    for (const stage of ['vertex', 'fragment']) {
      const header = lines.findIndex(line => line.includes(`${stage} translated:`));
      expect(header >= 0).toBe(stages.includes(stage));
      if (header < 0) continue;
      expect(header).toBeLessThan(at - 1);
      if (driver >= 0) expect(header).toBeLessThan(driver);
      if (mode === 'vertex' || mode === 'fragment') expect(lines[header]).toMatch(new RegExp(`${stage} translated: empty$`));
      if (mode === 'no-translation') expect(lines[header]).toMatch(new RegExp(`${stage} translated: unavailable$`));
      if (mode === 'link') expect(lines[header + 1]).toMatch(new RegExp(`// ${stage} stage$`));
      if (mode === 'long-translation') expect(record).toContain(`${stage} translated: truncated, 11400 bytes`);
    }
    const state = await probe();
    expect(state.queries).toBe(mode === 'destroyed' ? 0 : 2);
    expect(state.translations).toBe(mode === 'no-translation' ? 0 : stages.length);
    if (mode === 'link') expect(lines.findLastIndex(line => line.includes('driver detail'))).toBeLessThan(at - 1);
  });
}
