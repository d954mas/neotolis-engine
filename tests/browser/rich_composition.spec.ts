import { test, expect, type Page } from '@playwright/test';

type Rect = { x: number; y: number; width: number; height: number };
type SmokeWindow = Window & {
  __nt?: { ready: boolean; drawn_frames(): number; programs_ready(): boolean };
};
type Row = 'text' | 'image' | 'object';

test.use({ viewport: { width: 1280, height: 800 }, deviceScaleFactor: 1 });

async function pixels(page: Page, rect: Rect): Promise<number[]> {
  const png = await page.screenshot({ clip: rect });
  return page.evaluate(async (data) => {
    const image = new Image();
    await new Promise<void>((resolve, reject) => {
      image.onload = () => resolve();
      image.onerror = () => reject(new Error('rich witness PNG decode failed'));
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

function matchingPixels(actual: number[], expected: number[]): boolean {
  return actual.length === expected.length && actual.every((value, i) => Math.abs(value - expected[i]) <= 1);
}

function coloredPixels(data: number[], row: Row): number {
  let count = 0;
  for (let i = 0; i < data.length; i += 4) {
    const [r, g, b] = data.slice(i, i + 3);
    if (row === 'text' ? b > 150 && g > 100 && r < 100 : row === 'image' ? r > 200 && g < 100 && b < 100 : g > 200 && r < 100 && b < 100) count++;
  }
  return count;
}

async function loadWitness(page: Page, mode: 'clip' | 'open', effect = true): Promise<{ x: number; y: number }> {
  await page.goto('/index.html?rich_composition=' + mode + (effect ? '' : '&rich_effect=off'));
  await page.waitForFunction(() => {
    const hooks = (window as SmokeWindow).__nt;
    return hooks?.ready && hooks.programs_ready() && hooks.drawn_frames() >= 4;
  }, null, { timeout: 30_000 });
  const canvas = await page.locator('canvas').boundingBox();
  expect(canvas).not.toBeNull();
  expect(canvas!.width).toBe(1280);
  expect(canvas!.height).toBe(800);
  return { x: canvas!.x, y: canvas!.y };
}

test('rich composition: TEXT, IMAGE and OBJECT pixels keep the callback offset and obey the clip', async ({ page }) => {
  test.setTimeout(120_000);
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error' || /\b(abort(?:ed)?|(?:GL_)?INVALID_\w+|(?:GL_)?OUT_OF_MEMORY)\b/i.test(message.text())) errors.push(message.text());
  });
  const rows: Row[] = ['text', 'image', 'object'];
  const rect = (origin: { x: number; y: number }, row: number, x: number, width: number): Rect => ({
    x: Math.round(origin.x + x), y: Math.round(origin.y + 320 + row * 56), width, height: 48,
  });

  const clippedOrigin = await loadWitness(page, 'clip');
  const inside: number[][] = [];
  const outside: number[][] = [];
  for (let row = 0; row < rows.length; row++) {
    inside.push(await pixels(page, rect(clippedOrigin, row, 104, 152)));
    outside.push(await pixels(page, rect(clippedOrigin, row, 273, 15)));
    expect(coloredPixels(inside[row], rows[row]), rows[row] + ' really rendered inside the clip').toBeGreaterThan(80);
    expect(coloredPixels(outside[row], rows[row]), rows[row] + ' does not escape the clip').toBe(0);
  }

  const openOrigin = await loadWitness(page, 'open');
  const shiftedLeft: number[][] = [];
  const shiftedBody: number[][] = [];
  for (let row = 0; row < rows.length; row++) {
    expect(matchingPixels(await pixels(page, rect(openOrigin, row, 104, 152)), inside[row]), rows[row] + ' retained pixels are unchanged by clipping').toBe(true);
    const openOutside = await pixels(page, rect(openOrigin, row, 273, 15));
    expect(coloredPixels(openOutside, rows[row]), rows[row] + ' unclipped control extends past the boundary').toBeGreaterThan(80);
    expect(matchingPixels(openOutside, outside[row]), rows[row] + ' clip oracle is sensitive').toBe(false);
    shiftedLeft.push(await pixels(page, rect(openOrigin, row, 82, 12)));
    shiftedBody.push(await pixels(page, rect(openOrigin, row, 112, 128)));
    expect(coloredPixels(shiftedLeft[row], rows[row]), rows[row] + ' callback moved the leading edge by 16 px').toBe(0);
  }

  const noEffectOrigin = await loadWitness(page, 'open', false);
  for (let row = 0; row < rows.length; row++) {
    const unshifted = await pixels(page, rect(noEffectOrigin, row, 82, 12));
    expect(coloredPixels(unshifted, rows[row]), rows[row] + ' no-effect control restores the leading pixels').toBeGreaterThan(20);
    expect(matchingPixels(unshifted, shiftedLeft[row]), rows[row] + ' effect oracle is sensitive').toBe(false);
    const unshiftedBody = await pixels(page, rect(noEffectOrigin, row, 96, 128));
    expect(matchingPixels(unshiftedBody, shiftedBody[row]), rows[row] + ' callback offset is exactly 16 px').toBe(true);
  }
  expect(errors).toEqual([]);
});
