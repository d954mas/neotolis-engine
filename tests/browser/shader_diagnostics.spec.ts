import { test, expect } from '@playwright/test';

type Mode = 'vertex' | 'fragment' | 'link' | 'no-translation' | 'long-translation' | 'destroyed' | 'lost' | 'loss-in-log' | 'loss-in-reflection-log';
type Probe = { armed: boolean; queries: number; programLogs: number; translations: number; enables: number; failed: boolean; checkedLive: boolean; losses: number; restores: number };
type ProbeWindow = Window & {
  __nt?: { ready: boolean; programs_ready(): boolean; loss_window(step: number): number };
  __stages: Probe;
};

for (const mode of ['vertex', 'fragment', 'link', 'no-translation', 'long-translation', 'destroyed', 'lost', 'loss-in-log', 'loss-in-reflection-log'] as Mode[]) {
  test(`shader diagnostics: ${mode}`, async ({ page }) => {
    await page.addInitScript((mode: Mode) => {
      const state = (window as ProbeWindow).__stages = { armed: mode !== 'destroyed', queries: 0, programLogs: 0, translations: 0, enables: 0, failed: false, checkedLive: false, losses: 0, restores: 0 };
      const proto = WebGL2RenderingContext.prototype;
      const source = proto.shaderSource;
      const parameter = proto.getShaderParameter;
      const programParameter = proto.getProgramParameter;
      const link = proto.linkProgram;
      const lost = proto.isContextLost;
      const extension = proto.getExtension;
      const infoLog = proto.getShaderInfoLog;
      const error = proto.getError;
      // A real loss inside a diagnostic query, after the engine saw the context live.
      function loseDuringLog(gl: WebGL2RenderingContext) {
        const recovery = extension.call(gl, 'WEBGL_lose_context')!;
        gl.canvas.addEventListener('webglcontextlost', () => setTimeout(() => recovery.restoreContext(), 100), { once: true });
        gl.canvas.addEventListener('webglcontextrestored', () => state.restores++, { once: true });
        state.losses++;
        recovery.loseContext();
      }
      // Debug Emscripten builds read each parameter twice, so count stages, not calls.
      const queried = new Set<WebGLShader>();
      let target: WebGLProgram | null = null;
      proto.shaderSource = function(shader, text) {
        const type = parameter.call(this, shader, this.SHADER_TYPE);
        const broken = state.armed && ((mode === 'vertex' || mode === 'destroyed') && type === this.VERTEX_SHADER || mode === 'fragment' && type === this.FRAGMENT_SHADER);
        return source.call(this, shader, broken ? '#version 300 es\ninvalid shader source' : text);
      };
      proto.linkProgram = function(program) {
        if (state.armed && !target) target = program;
        return link.call(this, program);
      };
      proto.getProgramParameter = function(program, query) {
        if (program === target && mode === 'loss-in-reflection-log') {
          if (query !== this.ACTIVE_UNIFORMS) return programParameter.call(this, program, query);
          state.failed = true;
          return -1;
        }
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
      proto.isContextLost = function() {
        const result = mode === 'lost' && state.failed || lost.call(this);
        if (state.failed && !result) state.checkedLive = true;
        return result;
      };
      proto.getError = function() {
        if (mode === 'loss-in-reflection-log' && state.checkedLive && !state.losses) loseDuringLog(this);
        return error.call(this);
      };
      proto.getProgramInfoLog = function(program) {
        if (state.failed) state.programLogs++;
        if (program === target && mode === 'loss-in-log' && state.checkedLive && !state.losses) loseDuringLog(this);
        return program === target && mode === 'link' ? ('driver detail ' + 'x'.repeat(80) + '\n').repeat(20) : '';
      };
      // 'destroyed' passes the driver's log through to prove the held stage stays queryable; other modes keep stage logs out.
      proto.getShaderInfoLog = function(shader) { return mode === 'destroyed' ? infoLog.call(this, shader) : ''; };
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
    if (mode === 'loss-in-log' || mode === 'loss-in-reflection-log') {
      // The loss keeps the recoverable outcome: restore, relink, no trap.
      await page.waitForFunction(() => {
        const w = window as ProbeWindow;
        return w.__stages.restores === 1 && w.__nt?.ready && w.__nt.programs_ready();
      });
      expect(await probe()).toMatchObject({ failed: true, checkedLive: true, losses: 1, restores: 1 });
      expect(await crash()).toBeNull();
      return;
    }
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
    // A pending link holds stages whose handles were destroyed, so they report like live ones.
    const vertex = mode === 'vertex' || mode === 'destroyed' ? 0 : 1;
    const fragment = mode === 'fragment' ? 0 : 1;
    expect(lines[at - 1]).toContain(`program link stages: vs_compile=${vertex} fs_compile=${fragment}`);

    // With the extension, every compiled stage is translated; one the browser rejected has no text and logs nothing.
    const translated = mode === 'no-translation' ? [] : vertex === 0 ? ['fragment'] : fragment === 0 ? ['vertex'] : ['vertex', 'fragment'];
    const driver = lines.findIndex(line => line.includes('driver detail'));
    for (const stage of ['vertex', 'fragment']) {
      const header = lines.findIndex(line => line.endsWith(`${stage} translated:`));
      expect(header >= 0).toBe(translated.includes(stage));
      if (header < 0) continue;
      expect(header).toBeLessThan(at - 1);
      if (driver >= 0) expect(header).toBeLessThan(driver);
      if (mode !== 'long-translation') expect(lines[header + 1]).toMatch(new RegExp(`// ${stage} stage$`));
    }
    if (mode === 'long-translation') {
      // 11400 bytes per stage, cut to 2 KB each.
      const kept = lines.filter(line => line.includes('translated line')).length;
      expect(kept).toBeGreaterThan(60);
      expect(kept).toBeLessThanOrEqual(72);
    }
    if (mode === 'destroyed') {
      const header = lines.findIndex(line => line.endsWith('vertex shader:'));
      expect(header).toBeGreaterThan(0);
      // Record lines carry an `ERROR [gfx] ` prefix; the driver text after it names the broken token.
      expect(lines[header + 1].replace(/^ERROR \[[^\]]*\] /, '')).toMatch(/invalid/);
    }
    const state = await probe();
    expect(state.queries).toBe(2);
    expect(state.translations).toBe(mode === 'no-translation' ? 0 : 2);
    if (mode === 'link') expect(lines.findLastIndex(line => line.includes('driver detail'))).toBeLessThan(at - 1);
  });
}
