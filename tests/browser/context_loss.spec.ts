import { test, expect, type Page } from '@playwright/test';

declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      drawn_frames(): number;
      programs_ready(): boolean;
      field_css(): { x: number; y: number; w: number; h: number };
      hide_probe(mode: number): void;
    };
    __ntLossExtension?: WEBGL_lose_context;
    __ntReflectionLossInjected?: boolean;
  }
}

type Rect = { x: number; y: number; width: number; height: number };

/* The two SDK reflection paths that call getActiveUniform more than once, named
 * by the frame the pinned emsdk puts on the stack. This is the one place these
 * names live: an emsdk bump that renames or inlines either one is a two-string
 * edit, and every test below asserts its hook armed so the bump fails saying so
 * instead of timing out like a flake. */
const REFLECTION_FRAMES = {
  'max-length': '_emscripten_glGetProgramiv',
  'uniform-location': 'webglPrepareUniformLocationsBeforeFirstUse',
} as const;

const armedMessage = (query: keyof typeof REFLECTION_FRAMES) =>
  `reflection hook never armed: no getActiveUniform call came through '${REFLECTION_FRAMES[query]}' -- the pinned emsdk likely renamed or inlined it`;

test.use({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 });

async function capturePixels(page: Page, rect: Rect): Promise<number[]> {
  const png = await page.screenshot({ clip: rect });
  return page.evaluate(async (data) => {
    const image = new Image();
    await new Promise<void>((resolve, reject) => {
      image.onload = () => resolve();
      image.onerror = () => reject(new Error('probe PNG decode failed'));
      image.src = 'data:image/png;base64,' + data;
    });
    const canvas = document.createElement('canvas');
    canvas.width = image.width;
    canvas.height = image.height;
    const ctx = canvas.getContext('2d')!;
    ctx.drawImage(image, 0, 0);
    return Array.from(ctx.getImageData(0, 0, canvas.width, canvas.height).data);
  }, png.toString('base64'));
}

function pixelsMatch(actual: number[], expected: number[]): boolean {
  return actual.length === expected.length && actual.every((value, i) => Math.abs(value - expected[i]) <= 1);
}

function expectVisibleProbes(sprite: number[], text: number[]): void {
  let skinPixels = 0;
  for (let i = 0; i < sprite.length; i += 4) {
    if (Math.abs(sprite[i] - 56) <= 1 && Math.abs(sprite[i + 1] - 52) <= 1 && Math.abs(sprite[i + 2] - 48) <= 1) skinPixels++;
  }
  expect(skinPixels, 'sprite probe must contain the authored idle skin color').toBeGreaterThan((sprite.length / 4) * 0.9);
  let glyphPixels = 0;
  for (let i = 0; i < text.length; i += 4) {
    if (text[i] > 80 && text[i + 1] > 80 && text[i + 2] > 80) glyphPixels++;
  }
  expect(glyphPixels, 'caption probe must contain glyphs contrasting with the clear color').toBeGreaterThan(30);
}

test('context loss: both renderers restore their pixels after two loss cycles', async ({ page }) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    const text = message.text();
    if (text === 'ERROR [gfx] WebGL context lost') return;
    if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(text)) errors.push(text);
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready(), null, { timeout: 30_000 });

  const freshFrame = async () => {
    const before = await page.evaluate(() => window.__nt!.drawn_frames());
    await page.waitForFunction((n) => window.__nt!.drawn_frames() > n, before, { timeout: 30_000 });
  };
  const frame = () => page.evaluate(() => new Promise<void>((resolve) => requestAnimationFrame(() => requestAnimationFrame(() => resolve()))));
  await freshFrame();

  const canvas = await page.locator('canvas').boundingBox();
  expect(canvas).not.toBeNull();
  const field = await page.evaluate(() => window.__nt!.field_css());
  expect(field.w).toBe(320);
  expect(field.h).toBe(40);
  // Right-side skin excludes the input text/caret; the caption is above the field.
  const spriteRect = { x: Math.round(canvas!.x + field.x + field.w / 2 - 32), y: Math.round(canvas!.y + field.y - 5), width: 20, height: 10 };
  const textRect = { x: Math.round(canvas!.x + 24), y: Math.round(canvas!.y + 24), width: 230, height: 24 };
  const spriteBaseline = await capturePixels(page, spriteRect);
  const textBaseline = await capturePixels(page, textRect);
  expectVisibleProbes(spriteBaseline, textBaseline);

  // These controls prove pixel-matcher sensitivity, not a simulated recovery failure.
  for (const mode of [1, 2]) {
    await test.step('pixel control: hide only ' + (mode === 1 ? 'sprite skin' : 'caption glyphs'), async () => {
      await page.evaluate((value) => window.__nt!.hide_probe(value), mode);
      await freshFrame();
      expect(await page.evaluate(() => window.__nt!.programs_ready())).toBe(true);
      expect(pixelsMatch(await capturePixels(page, spriteRect), spriteBaseline), 'sprite probe sensitivity').toBe(mode !== 1);
      expect(pixelsMatch(await capturePixels(page, textRect), textBaseline), 'text probe sensitivity').toBe(mode !== 2);
      await page.evaluate(() => window.__nt!.hide_probe(0));
      await freshFrame();
      expect(pixelsMatch(await capturePixels(page, spriteRect), spriteBaseline)).toBe(true);
      expect(pixelsMatch(await capturePixels(page, textRect), textBaseline)).toBe(true);
    });
  }

  for (let cycle = 0; cycle < 2; cycle++) {
    await test.step('context loss/restore ' + (cycle + 1), async () => {
      const hasExtension = await page.evaluate(() => {
        const gl = document.querySelector('canvas')!.getContext('webgl2')!;
        window.__ntLossExtension = gl.getExtension('WEBGL_lose_context') ?? undefined;
        window.__ntLossExtension?.loseContext();
        return window.__ntLossExtension !== undefined;
      });
      expect(hasExtension, 'WEBGL_lose_context unavailable').toBe(true);
      await page.waitForFunction(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !window.__nt!.programs_ready(), null, { timeout: 10_000 });
      const stopped = await page.evaluate(() => window.__nt!.drawn_frames());
      await frame();
      await frame();
      expect(await page.evaluate(() => window.__nt!.programs_ready())).toBe(false);
      expect(await page.evaluate(() => window.__nt!.drawn_frames()), 'lost context must not draw before explicit restore').toBe(stopped);

      await page.evaluate(() => window.__ntLossExtension!.restoreContext());
      await page.waitForFunction(() => window.__nt!.programs_ready(), null, { timeout: 30_000 });
      await freshFrame();
      const sprite = await capturePixels(page, spriteRect);
      const text = await capturePixels(page, textRect);
      expectVisibleProbes(sprite, text);
      expect(pixelsMatch(sprite, spriteBaseline), 'sprite pixels after restore').toBe(true);
      expect(pixelsMatch(text, textBaseline), 'text pixels after restore').toBe(true);
      expect(errors, 'unexpected browser/gfx errors').toEqual([]);
    });
  }
  expect(errors, 'unexpected browser/gfx errors').toEqual([]);
});

for (const query of ['max-length', 'uniform-location'] as const) {
  test('SDK reflection reports errors unrelated to context loss: ' + query, async ({ page }) => {
    await page.goto('/index.html');
    await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready());
    await page.evaluate((target) => {
      const gl = document.querySelector('canvas')!.getContext('webgl2')!;
      window.__ntLossExtension = gl.getExtension('WEBGL_lose_context')!;
      const original = gl.getActiveUniform;
      gl.getActiveUniform = function (program, index) {
        const stack = new Error().stack ?? '';
        if (stack.includes(target)) {
          gl.getActiveUniform = original;
          window.__ntReflectionLossInjected = true;
          throw new Error('injected non-loss reflection error');
        }
        return original.call(this, program, index);
      };
      window.__ntLossExtension!.loseContext();
    }, REFLECTION_FRAMES[query]);
    await page.waitForFunction(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !window.__nt!.programs_ready());
    const error = page.waitForEvent('pageerror', { timeout: 20_000 }).catch(() => null);
    await page.evaluate(() => window.__ntLossExtension!.restoreContext());
    const raised = await error;
    /* Checked before the message: a rename in the pinned emsdk means the hook never
     * armed, and without this that reads as an unexplained event timeout. */
    expect(await page.evaluate(() => window.__ntReflectionLossInjected === true), armedMessage(query)).toBe(true);
    expect(raised?.message).toBe('injected non-loss reflection error');
    expect(await page.evaluate(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost())).toBe(false);
  });

  test('context loss inside SDK reflection: ' + query, async ({ page }) => {
    test.setTimeout(90_000);
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    page.on('console', (message) => {
      const text = message.text();
      if (text === 'ERROR [gfx] WebGL context lost') return;
      if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(text)) errors.push(text);
    });
    await page.goto('/index.html');
    await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready());
    const drawnBefore = await page.evaluate(() => window.__nt!.drawn_frames());

    await page.evaluate((target) => {
      const gl = document.querySelector('canvas')!.getContext('webgl2')!;
      window.__ntLossExtension = gl.getExtension('WEBGL_lose_context')!;
      const original = gl.getActiveUniform;
      gl.getActiveUniform = function (program, index) {
        const stack = new Error().stack ?? '';
        if (stack.includes(target)) {
          gl.getActiveUniform = original;
          window.__ntReflectionLossInjected = true;
          window.__ntLossExtension!.loseContext();
        }
        return original.call(this, program, index);
      };
      window.__ntLossExtension!.loseContext();
    }, REFLECTION_FRAMES[query]);
    await page.waitForFunction(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !window.__nt!.programs_ready());
    await page.evaluate(() => window.__ntLossExtension!.restoreContext());
    await page.waitForFunction(() => window.__ntReflectionLossInjected === true, undefined, { timeout: 20_000 }).catch((cause: Error) => {
      /* Keep the original: a crashed page or a closed context must not be
       * misread as an emsdk rename and send the next reader to edit the const. */
      throw new Error(`${armedMessage(query)} (underlying: ${cause.message})`);
    });
    await page.waitForFunction(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !window.__nt!.programs_ready());
    expect(errors, 'reflection interrupted by context loss must not throw').toEqual([]);

    const stopped = await page.evaluate(() => window.__nt!.drawn_frames());
    await page.evaluate(() => window.__ntLossExtension!.restoreContext());
    await page.waitForFunction((before) => window.__nt!.programs_ready() && window.__nt!.drawn_frames() > before, Math.max(drawnBefore, stopped));
    const canvas = (await page.locator('canvas').boundingBox())!;
    const field = await page.evaluate(() => window.__nt!.field_css());
    const sprite = await capturePixels(page, { x: Math.round(canvas.x + field.x + field.w / 2 - 32), y: Math.round(canvas.y + field.y - 5), width: 20, height: 10 });
    const text = await capturePixels(page, { x: Math.round(canvas.x + 24), y: Math.round(canvas.y + 24), width: 230, height: 24 });
    expectVisibleProbes(sprite, text);
    expect(errors, 'fresh frames after recovery must not report browser/gfx errors').toEqual([]);
  });
}
