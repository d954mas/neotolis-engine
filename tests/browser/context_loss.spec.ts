import { test, expect, type Page } from '@playwright/test';

declare global {
  interface Window {
    __nt?: {
      ready: boolean;
      drawn_frames(): number;
      programs_ready(): boolean;
      float_texture_linear(): boolean;
      field_css(): { x: number; y: number; w: number; h: number };
      hide_probe(mode: number): void;
      basis_ready(): boolean;
      basis_format(): number;
      basis_rgb_format(): number;
      basis_caps(): number;
      basis_build_targets(): number;
      basis_build_codecs(): number;
      basis_sample(level: number): number;
      basis_single_pixel_format(): number;
    };
    __ntLossExtension?: WEBGL_lose_context;
    __ntBlockFloatLinear?: boolean;
    __ntProbeUploads?: number;
  }
}

type Rect = { x: number; y: number; width: number; height: number };

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

function expectMeshProbe(pixels: number[]): void {
  let colored = 0;
  for (let i = 0; i < pixels.length; i += 4) {
    if (Math.abs(pixels[i] - 64) <= 1 && Math.abs(pixels[i + 1] - 128) <= 1 && Math.abs(pixels[i + 2] - 191) <= 1) colored++;
  }
  expect(colored, 'mesh probe must preserve its nonzero vec4 after program recreation').toBeGreaterThan((pixels.length / 4) * 0.9);
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
  await page.addInitScript(() => {
    const locations = new WeakSet<WebGLUniformLocation>();
    const getLocation = WebGL2RenderingContext.prototype.getUniformLocation;
    WebGL2RenderingContext.prototype.getUniformLocation = function(program, name) {
      const location = getLocation.call(this, program, name);
      if (name === 'u_probe_color' && location) locations.add(location);
      return location;
    };
    const upload = WebGL2RenderingContext.prototype.uniform4fv;
    window.__ntProbeUploads = 0;
    WebGL2RenderingContext.prototype.uniform4fv = function(location, data, offset?, length?) {
      if (location && locations.has(location)) window.__ntProbeUploads!++;
      upload.call(this, location, data, offset, length);
    };
    const getExtension = WebGL2RenderingContext.prototype.getExtension;
    WebGL2RenderingContext.prototype.getExtension = function(name: string) {
      if (name === 'OES_texture_float_linear' && window.__ntBlockFloatLinear) return null;
      return getExtension.call(this, name);
    };
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready(), null, { timeout: 30_000 });
  expect(await page.evaluate(() => window.__nt!.float_texture_linear()), 'SwiftShader supports float filtering at init').toBe(true);

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
  // The wasm app's mesh probe: two instanced colored quads in the bottom-right
  // corner drawn through an owned vertex input (the WebGL2 VAO path). One rect
  // per quad -- the second (y 688..728) fails if only instance 0 is drawn.
  const meshRect = { x: Math.round(canvas!.x + 1194), y: Math.round(canvas!.y + 748), width: 40, height: 24 };
  const meshRect2 = { x: Math.round(canvas!.x + 1194), y: Math.round(canvas!.y + 696), width: 40, height: 24 };
  const spriteBaseline = await capturePixels(page, spriteRect);
  const textBaseline = await capturePixels(page, textRect);
  expectVisibleProbes(spriteBaseline, textBaseline);
  expectMeshProbe(await capturePixels(page, meshRect));
  expectMeshProbe(await capturePixels(page, meshRect2));

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

  expect(await page.evaluate(() => window.__ntProbeUploads)).toBe(1);

  for (let cycle = 0; cycle < 2; cycle++) {
    await test.step('context loss/restore ' + (cycle + 1), async () => {
      // Change availability across restores to catch a stale capability cache.
      await page.evaluate((blocked) => { window.__ntBlockFloatLinear = blocked; }, cycle === 0);
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
      expect(await page.evaluate(() => window.__nt!.float_texture_linear()), 'capability must reflect the restored context').toBe(cycle !== 0);
      await freshFrame();
      const sprite = await capturePixels(page, spriteRect);
      const text = await capturePixels(page, textRect);
      expectVisibleProbes(sprite, text);
      expectMeshProbe(await capturePixels(page, meshRect));
      expectMeshProbe(await capturePixels(page, meshRect2));
      expect(await page.evaluate(() => window.__ntProbeUploads), 'first upload after each restore must reach GL once').toBe(cycle + 2);
      expect(pixelsMatch(sprite, spriteBaseline), 'sprite pixels after restore').toBe(true);
      expect(pixelsMatch(text, textBaseline), 'text pixels after restore').toBe(true);
      expect(errors, 'unexpected browser/gfx errors').toEqual([]);
    });
  }
  expect(errors, 'unexpected browser/gfx errors').toEqual([]);
});



// nt_texture_format_t values the activator can pick for the fixture textures.
const FORMAT_RGBA8 = 1;
const FORMAT_ETC2_RGB8 = 11;
const FORMAT_ETC2_RGBA8 = 12;
const FORMAT_BC7_RGBA = 13;
const FORMAT_ASTC_4x4_RGBA = 14;

// The activator's per-codec order (ETC1S: ETC2 -> BC7 -> ASTC; UASTC: ASTC -> BC7 -> ETC2) over the
// formats both the GPU reports and the build admits (NT_BASISU_HAS_ETC2/BC7/ASTC); an opaque texture
// takes ETC2 RGB8. RGBA8 is always the last candidate. Bits: 1 = BC7, 2 = ASTC, 4 = ETC2.
const CODEC_ETC1S = 1;
const CODEC_UASTC = 2;
function expectedBasisFormat(caps: number, buildTargets: number, hasAlpha: boolean, codec: number): number {
  const admitted = caps & buildTargets;
  const etc2 = hasAlpha ? FORMAT_ETC2_RGBA8 : FORMAT_ETC2_RGB8;
  const order = codec === CODEC_ETC1S ? [[4, etc2], [1, FORMAT_BC7_RGBA], [2, FORMAT_ASTC_4x4_RGBA]] : [[2, FORMAT_ASTC_4x4_RGBA], [1, FORMAT_BC7_RGBA], [4, etc2]];
  for (const [bit, format] of order) if (admitted & bit) return format;
  return FORMAT_RGBA8;
}

function unpack(sample: number): number[] {
  return [sample & 0xff, (sample >>> 8) & 0xff, (sample >>> 16) & 0xff, (sample >>> 24) & 0xff];
}

function expectTexel(sample: number, expected: number[], tolerance: number, label: string): void {
  const actual = unpack(sample);
  for (let c = 0; c < 4; c++) {
    expect(Math.abs(actual[c] - expected[c]), `${label}: channel ${c} of rgba(${actual}) vs (${expected})`).toBeLessThanOrEqual(tolerance);
  }
}

// The RGBA fixture is 128x128: left half (200,40,40,255), right half (40,40,200,128). Levels 0 and 3
// sample texel (0,0) inside the left half; the 1x1 level 7 is the linear average of both halves.
// Compressed targets approximate solid blocks, hence the tolerances: the level-3 corner measures
// 13 off in red for UASTC through ASTC (the codec's own content, not a transcode) under SwiftShader,
// so 16 keeps every real target while a channel swap or a flip still misses by 160 and the 1x1
// average by 80.
const FIXTURE_LEFT = [200, 40, 40, 255];
const FIXTURE_AVERAGE = [120, 40, 120, 191];
const FIXTURE_TOLERANCE = 16;

async function checkBasisFixture(page: Page, label: string): Promise<{ corner: number; middle: number; last: number }> {
  await page.waitForFunction(() => window.__nt!.basis_ready(), null, { timeout: 30_000 });
  const caps = await page.evaluate(() => window.__nt!.basis_caps());
  const buildTargets = await page.evaluate(() => window.__nt!.basis_build_targets());
  // The fixture pack is ETC1S whenever the build admits it, UASTC otherwise.
  const codec = ((await page.evaluate(() => window.__nt!.basis_build_codecs())) & CODEC_ETC1S) ? CODEC_ETC1S : CODEC_UASTC;
  const format = await page.evaluate(() => window.__nt!.basis_format());
  const rgbFormat = await page.evaluate(() => window.__nt!.basis_rgb_format());
  const corner = await page.evaluate(() => window.__nt!.basis_sample(0));
  const middle = await page.evaluate(() => window.__nt!.basis_sample(3));
  const last = await page.evaluate(() => window.__nt!.basis_sample(7));
  console.log(`[basis ${label}] caps=${caps} build_targets=${buildTargets} codec=${codec} format=${format} rgb_format=${rgbFormat} corner=0x${corner.toString(16)} middle=0x${middle.toString(16)} last=0x${last.toString(16)}`);
  expect(format, `${label}: transcode target for caps ${caps}, build targets ${buildTargets} and codec ${codec}`).toBe(expectedBasisFormat(caps, buildTargets, true, codec));
  expect(rgbFormat, `${label}: opaque transcode target for caps ${caps}, build targets ${buildTargets} and codec ${codec}`).toBe(expectedBasisFormat(caps, buildTargets, false, codec));
  expectTexel(corner, FIXTURE_LEFT, FIXTURE_TOLERANCE, `${label}: level-0 corner texel`);
  expectTexel(middle, FIXTURE_LEFT, FIXTURE_TOLERANCE, `${label}: level-3 corner texel`);
  expectTexel(last, FIXTURE_AVERAGE, 20, `${label}: 1x1 last level`);
  return { corner, middle, last };
}

test('basis fixture: the transcoded textures keep their format and texels across a context loss', async ({ page }) => {
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
  const before = await checkBasisFixture(page, 'initial');

  const hasExtension = await page.evaluate(() => {
    const gl = document.querySelector('canvas')!.getContext('webgl2')!;
    window.__ntLossExtension = gl.getExtension('WEBGL_lose_context') ?? undefined;
    window.__ntLossExtension?.loseContext();
    return window.__ntLossExtension !== undefined;
  });
  expect(hasExtension, 'WEBGL_lose_context unavailable').toBe(true);
  await page.waitForFunction(() => document.querySelector('canvas')!.getContext('webgl2')!.isContextLost() && !window.__nt!.programs_ready(), null, { timeout: 10_000 });
  // Observe the lost edge before restoring: gfx clears texture backends on loss,
  // so readiness is false here and true again only after re-activation. Polling
  // for the false edge after restore could miss it if the runner pauses.
  await page.waitForFunction(() => !window.__nt!.basis_ready(), null, { timeout: 30_000 });
  await page.evaluate(() => window.__ntLossExtension!.restoreContext());
  await page.waitForFunction(() => window.__nt!.programs_ready(), null, { timeout: 30_000 });

  // Re-activation runs off the same pack blob, so the transcode is bit-identical.
  const after = await checkBasisFixture(page, 'after restore');
  expect(after, 'texels after re-activation').toEqual(before);
  expect(errors, 'unexpected browser/gfx errors').toEqual([]);
});

test('basis fixture: a single pixel skips BC7 level-zero restrictions', async ({ page }) => {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    const text = message.text();
    if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(text)) errors.push(text);
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready(), null, { timeout: 30_000 });
  const buildTargets = await page.evaluate(() => window.__nt!.basis_build_targets());
  const buildCodecs = await page.evaluate(() => window.__nt!.basis_build_codecs());
  // The embedded blob is UASTC and the restriction under test is BC7's. The 1x1 observes it only
  // where BC7 is UASTC's first candidate: no ASTC admitted and reported.
  test.skip((buildTargets & 1) === 0 || (buildCodecs & 2) === 0, 'build admits no BC7 target or no UASTC codec');
  const caps = await page.evaluate(() => window.__nt!.basis_caps());
  expect(caps & 1, 'BC7 must be available to exercise its level-zero restriction').toBe(1);
  test.skip((caps & buildTargets & 2) !== 0, 'ASTC precedes BC7 for UASTC; the level-zero rule is unobservable here');
  const format = await page.evaluate(() => window.__nt!.basis_single_pixel_format());
  const expected = expectedBasisFormat(caps & ~1, buildTargets, true, CODEC_UASTC);
  expect(errors, 'single-pixel Basis activation must not emit WebGL errors').toEqual([]);
  expect(format, 'single-pixel Basis activation selects the next supported target').toBe(expected);
});

test('vec4 pixel probe detects an omitted initial upload', async ({ page }) => {
  await page.addInitScript(() => {
    const locations = new WeakSet<WebGLUniformLocation>();
    const getLocation = WebGL2RenderingContext.prototype.getUniformLocation;
    WebGL2RenderingContext.prototype.getUniformLocation = function(program, name) {
      const location = getLocation.call(this, program, name);
      if (name === 'u_probe_color' && location) locations.add(location);
      return location;
    };
    const upload = WebGL2RenderingContext.prototype.uniform4fv;
    WebGL2RenderingContext.prototype.uniform4fv = function(location, data, offset?, length?) {
      if (location && locations.has(location)) return;
      upload.call(this, location, data, offset, length);
    };
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready() && window.__nt.drawn_frames() > 3);
  const canvas = await page.locator('canvas').boundingBox();
  expect(canvas).not.toBeNull();
  const pixels = await capturePixels(page, { x: Math.round(canvas!.x + 1194), y: Math.round(canvas!.y + 748), width: 40, height: 24 });
  let black = 0;
  for (let i = 0; i < pixels.length; i += 4) {
    if (pixels[i] === 0 && pixels[i + 1] === 0 && pixels[i + 2] === 0) black++;
  }
  expect(black, 'without the upload GL retains default zero, visibly different from the expected color').toBeGreaterThan(pixels.length / 4 * 0.9);
});

test('context loss: a loss and restore between two frames still runs the restore path', async ({ page }) => {
  test.setTimeout(60_000);
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    const text = message.text();
    if (text === 'ERROR [gfx] WebGL context lost') return;
    if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(text)) errors.push(text);
  });
  // A background tab: animation frames stop while the browser loses and restores the context.
  await page.addInitScript(() => {
    const held: FrameRequestCallback[] = [];
    const request = window.requestAnimationFrame.bind(window);
    const control = { hold: false, release() { this.hold = false; held.splice(0).forEach((callback) => request(callback)); } };
    (window as unknown as { __ntFrames: typeof control }).__ntFrames = control;
    window.requestAnimationFrame = (callback) => {
      if (control.hold) {
        held.push(callback);
        return 0;
      }
      return request(callback);
    };
  });
  await page.goto('/index.html');
  await page.waitForFunction(() => window.__nt?.ready && window.__nt.programs_ready() && window.__nt.drawn_frames() > 2, null, { timeout: 30_000 });
  const cycled = await page.evaluate(async () => {
    const frames = (window as unknown as { __ntFrames: { hold: boolean; release(): void } }).__ntFrames;
    frames.hold = true;
    // Let the in-flight frame finish; later requests queue.
    await new Promise((resolve) => setTimeout(resolve, 100));
    const before = window.__nt!.drawn_frames();
    const canvas = document.querySelector('canvas')!;
    const loss = canvas.getContext('webgl2')!.getExtension('WEBGL_lose_context');
    if (!loss) throw new Error('WEBGL_lose_context unavailable');
    const lost = new Promise((resolve) => canvas.addEventListener('webglcontextlost', resolve, { once: true }));
    const restored = new Promise((resolve) => canvas.addEventListener('webglcontextrestored', resolve, { once: true }));
    const within = (promise: Promise<unknown>, stage: string) =>
      Promise.race([promise, new Promise((_, reject) => setTimeout(() => reject(new Error(stage + ' event did not arrive')), 5_000))]);
    loss.loseContext();
    await within(lost, 'webglcontextlost');
    // The browser allows a restore only after the lost event's dispatch finishes.
    await new Promise((resolve) => setTimeout(resolve, 0));
    loss.restoreContext();
    await within(restored, 'webglcontextrestored');
    const drawnWhileHeld = window.__nt!.drawn_frames() - before;
    frames.release();
    return { drawnWhileHeld, lostNow: canvas.getContext('webgl2')!.isContextLost() };
  });
  expect(cycled).toEqual({ drawnWhileHeld: 0, lostNow: false });
  await page.waitForFunction(() => (window as unknown as { __nt: { restore_ticks(): number } }).__nt.restore_ticks() > 0 && window.__nt!.programs_ready(), null, { timeout: 15_000 });
  expect(await page.evaluate(() => (window as unknown as { __nt: { restore_status(): number } }).__nt.restore_status()), 'the restore tick completes').toBe(1);
  expect(errors, 'unexpected browser/gfx errors').toEqual([]);
});
