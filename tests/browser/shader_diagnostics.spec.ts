import { test, expect } from '@playwright/test';

type Mode = 'vertex' | 'fragment' | 'both' | 'link' | 'long-log' | 'unavailable' | 'null-status' | 'lost' | 'success';
type ProbeWindow = Window & {
  __nt?: { ready: boolean; programs_ready(): boolean; loss_window(step: number): number };
  __stages: { armed: boolean; queries: number; deletedQueries: number; programLogs: number; failed: boolean; extensionQueries: number; sourceQueries: number };
};

for (const mode of ['vertex', 'fragment', 'both', 'link', 'long-log', 'unavailable', 'null-status', 'lost', 'success'] as Mode[]) {
  test(`shader diagnostics: ${mode}`, async ({ page }) => {
    await page.addInitScript((mode: Mode) => {
      const state = (window as ProbeWindow).__stages = { armed: !['unavailable', 'lost', 'success'].includes(mode), queries: 0, deletedQueries: 0, programLogs: 0, failed: false, extensionQueries: 0, sourceQueries: 0 };
      const proto = WebGL2RenderingContext.prototype;
      const source = proto.shaderSource;
      const parameter = proto.getShaderParameter;
      const programParameter = proto.getProgramParameter;
      const link = proto.linkProgram;
      const attached = proto.getAttachedShaders;
      const remove = proto.deleteShader;
      const lost = proto.isContextLost;
      const extension = proto.getExtension;
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
      proto.getExtension = function(name: string) {
        const result = extension.call(this, name);
        if (name !== 'WEBGL_debug_shaders') return result;
        state.extensionQueries++;
        if (!result) return result;
        return new Proxy(result, { get(target, property, receiver) {
          const value = Reflect.get(target, property, receiver);
          if (property !== 'getTranslatedShaderSource' || typeof value !== 'function') return value;
          return function(shader: WebGLShader) {
            state.sourceQueries++;
            return value.call(target, shader);
          };
        } });
      };
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
      expect(await page.evaluate(() => (window as ProbeWindow).__stages.extensionQueries)).toBe(0);
      expect(await page.evaluate(() => (window as ProbeWindow).__stages.sourceQueries)).toBe(0);
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
        expect(state.extensionQueries).toBe(0);
        expect(state.sourceQueries).toBe(0);
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
    if (mode === 'unavailable') {
      expect(state.extensionQueries).toBe(0);
      expect(state.sourceQueries).toBe(0);
    }
    if (mode === 'long-log') {
      expect(record).toContain('driver detail');
      expect(record.lastIndexOf(' link: link failed:')).toBeGreaterThan(record.lastIndexOf('driver detail'));
    }
  });
}

test('shader diagnostics: preserves bounded translated source chunks after a large driver log', async ({ page }) => {
  await page.addInitScript(() => {
    const state: any = (window as any).__translated = { extensionQueries: 0, sourceQueries: 0, failed: false };
    const proto = WebGL2RenderingContext.prototype;
    const shaderSource = proto.shaderSource;
    const shaderParameter = proto.getShaderParameter;
    const programParameter = proto.getProgramParameter;
    const linkProgram = proto.linkProgram;
    const attached = proto.getAttachedShaders;
    const getExtension = proto.getExtension;
    let target: WebGLProgram | null = null;
    const vertex = 'vertex:' + 'v'.repeat(700) + String.fromCharCode(92, 10, 13) + '😀';
    const fragment = 'fragment:' + 'f'.repeat(650) + String.fromCharCode(92, 10, 13) + 'é';
    state.vertex = vertex;
    state.fragment = fragment;
    proto.shaderSource = function(shader, text) {
      return shaderSource.call(this, shader, '#version 300 es\ninvalid shader source');
    };
    proto.linkProgram = function(program) {
      target ??= program;
      return linkProgram.call(this, program);
    };
    proto.getProgramParameter = function(program, name) {
      if (program === target && name === this.LINK_STATUS) {
        state.failed = true;
        return false;
      }
      return programParameter.call(this, program, name);
    };
    proto.getAttachedShaders = function(program) { return attached.call(this, program)?.reverse() ?? null; };
    proto.getProgramInfoLog = function(program) {
      return program === target ? ('driver detail ' + 'x'.repeat(80) + '\n').repeat(256) : '';
    };
    proto.getExtension = function(name: string) {
      if (name !== 'WEBGL_debug_shaders') return getExtension.call(this, name);
      state.extensionQueries++;
      return { getTranslatedShaderSource: (shader: WebGLShader) => {
        state.sourceQueries++;
        const type = shaderParameter.call(this, shader, this.SHADER_TYPE);
        return type === this.VERTEX_SHADER ? vertex : fragment;
      } };
    };
  });
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
  const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
  const chunks = (stage: string) => Array.from(record.matchAll(new RegExp(`translated_${stage}\\[(\\d+)\\]: (.*)`, 'g')))
    .sort((a, b) => Number(a[1]) - Number(b[1])).map(match => match[2]).join('');
  const decode = (text: string) => text.replace(/\\(\\|n|r)/g, (_, escape) => escape === 'n' ? '\n' : escape === 'r' ? '\r' : '\\');
  const state = await page.evaluate(() => (window as any).__translated);
  expect(chunks('vs')).toContain('vertex:');
  expect(chunks('fs')).toContain('fragment:');
  expect(chunks('vs').length).toBeGreaterThan(224);
  expect(chunks('fs').length).toBeGreaterThan(224);
  expect(decode(chunks('vs'))).toBe(state.vertex);
  expect(decode(chunks('fs'))).toBe(state.fragment);
  expect(record).not.toContain('translated_vs: <truncated>');
  expect(record).not.toContain('translated_fs: <truncated>');
  expect(record).toContain('driver detail');
  expect(record.indexOf('translated_vs[0]:')).toBeGreaterThan(record.indexOf('driver detail'));
  expect(record.lastIndexOf(' link: link failed:')).toBeGreaterThan(record.lastIndexOf('translated_fs[0]:'));
  expect(state.extensionQueries).toBe(1);
  expect(state.sourceQueries).toBe(2);
});

test('shader diagnostics: bounds escaped translated sources without corrupting UTF-8', async ({ page }) => {
  await page.addInitScript(() => {
    const proto = WebGL2RenderingContext.prototype;
    const shaderSource = proto.shaderSource;
    const programParameter = proto.getProgramParameter;
    const linkProgram = proto.linkProgram;
    const getExtension = proto.getExtension;
    let target: WebGLProgram | null = null;
    proto.shaderSource = function(shader, text) { return shaderSource.call(this, shader, '#version 300 es\ninvalid shader source'); };
    proto.linkProgram = function(program) { target ??= program; return linkProgram.call(this, program); };
    proto.getProgramParameter = function(program, name) { return program === target && name === this.LINK_STATUS ? false : programParameter.call(this, program, name); };
    proto.getExtension = function(name: string) {
      if (name !== 'WEBGL_debug_shaders') return getExtension.call(this, name);
      return { getTranslatedShaderSource: () => ('\\\\\n😀').repeat(900) };
    };
  });
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
  const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
  expect(record).toContain('translated_vs: <truncated>');
  expect(record).toContain('translated_fs: <truncated>');
  expect(record).toContain('\\\\\\\\\\n😀');
  expect(record).not.toContain('�');
  expect(record).toContain(' link: link failed:');
});

for (const result of ['unsupported', 'empty', 'throws', 'lookup-throws'] as const) {
  test(`shader diagnostics: translated source ${result} preserves the failure`, async ({ page }) => {
    await page.addInitScript((result) => {
      const proto = WebGL2RenderingContext.prototype;
      const shaderSource = proto.shaderSource;
      const programParameter = proto.getProgramParameter;
      const linkProgram = proto.linkProgram;
      const getExtension = proto.getExtension;
      let target: WebGLProgram | null = null;
      proto.shaderSource = function(shader, text) { return shaderSource.call(this, shader, '#version 300 es\ninvalid shader source'); };
      proto.linkProgram = function(program) { target ??= program; return linkProgram.call(this, program); };
      proto.getProgramParameter = function(program, name) { return program === target && name === this.LINK_STATUS ? false : programParameter.call(this, program, name); };
      proto.getExtension = function(name: string) {
        if (name !== 'WEBGL_debug_shaders') return getExtension.call(this, name);
        if (result === 'lookup-throws') throw new Error('extension lookup failure');
        if (result === 'unsupported') return null;
        return { getTranslatedShaderSource: () => {
          if (result === 'throws') throw new Error('extension query failure');
          return '';
        } };
      };
    }, result);
    await page.goto('/index.html?crash=read');
    await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
    const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
    const marker = result === 'unsupported' ? '<unsupported>' : result === 'empty' ? '<empty>' : '<unavailable>';
    expect(record).toContain(`translated_vs: ${marker}`);
    expect(record).toContain(`translated_fs: ${marker}`);
    expect(record).toContain('link failed: gl_error=0x0');
  });
}

test('shader diagnostics: captures real WEBGL_debug_shaders output when available', async ({ page }) => {
  await page.addInitScript(() => {
    const state = (window as any).__realTranslated = { extensionQueries: 0, sourceQueries: 0, available: false };
    const proto = WebGL2RenderingContext.prototype;
    const programParameter = proto.getProgramParameter;
    const linkProgram = proto.linkProgram;
    const getExtension = proto.getExtension;
    let target: WebGLProgram | null = null;
    proto.linkProgram = function(program) { target ??= program; return linkProgram.call(this, program); };
    proto.getProgramParameter = function(program, name) {
      return program === target && name === this.LINK_STATUS ? false : programParameter.call(this, program, name);
    };
    proto.getExtension = function(name: string) {
      const result = getExtension.call(this, name);
      if (name !== 'WEBGL_debug_shaders') return result;
      state.extensionQueries++;
      if (!result) return result;
      state.available = true;
      return new Proxy(result, { get(target, property, receiver) {
        const value = Reflect.get(target, property, receiver);
        if (property !== 'getTranslatedShaderSource' || typeof value !== 'function') return value;
        return function(shader: WebGLShader) {
          state.sourceQueries++;
          return value.call(target, shader);
        };
      } });
    };
  });
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
  const state = await page.evaluate(() => (window as any).__realTranslated);
  test.skip(!state.available, 'WEBGL_debug_shaders is unavailable in this browser');
  const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
  expect(record).toMatch(/translated_vs\[0\]: .+/);
  expect(record).toMatch(/translated_fs\[0\]: .+/);
  expect(state.extensionQueries).toBe(1);
  expect(state.sourceQueries).toBe(2);
});

test('shader diagnostics: retains the original GL error after translated-source queries', async ({ page }) => {
  await page.addInitScript(() => {
    const proto = WebGL2RenderingContext.prototype;
    const shaderSource = proto.shaderSource;
    const programParameter = proto.getProgramParameter;
    const linkProgram = proto.linkProgram;
    const getExtension = proto.getExtension;
    const getError = proto.getError;
    let target: WebGLProgram | null = null;
    let failed = false;
    let queried = false;
    proto.shaderSource = function(shader, text) { return shaderSource.call(this, shader, '#version 300 es\ninvalid shader source'); };
    proto.linkProgram = function(program) { target ??= program; return linkProgram.call(this, program); };
    proto.getProgramParameter = function(program, name) {
      if (program === target && name === this.LINK_STATUS) {
        failed = true;
        return false;
      }
      return programParameter.call(this, program, name);
    };
    proto.getError = function() {
      return !failed ? getError.call(this) : queried ? 0x500 : 0x502;
    };
    proto.getExtension = function(name: string) {
      if (name !== 'WEBGL_debug_shaders') return getExtension.call(this, name);
      return { getTranslatedShaderSource: () => {
        queried = true;
        return 'translated';
      } };
    };
  });
  await page.goto('/index.html?crash=read');
  await page.waitForFunction(() => localStorage.getItem('nt.test.crash') !== null);
  const record = (await page.evaluate(() => localStorage.getItem('nt.test.crash')))!;
  expect(record).toContain('translated_vs[0]: translated');
  expect(record).toContain('translated_fs[0]: translated');
  expect(record).toContain('link failed: gl_error=0x502');
});
