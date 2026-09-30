import { expect, test } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

const shaderDir = join(__dirname, '..', '..', 'assets', 'shaders');

function shaderSource(name: string): string {
  const expand = (file: string): string => readFileSync(join(shaderDir, file), 'utf8')
    .replace(/^#pragma once\s*$/gm, '')
    .replace(/^#include "([^"]+)"\s*$/gm, (_line, included: string) => expand(join(dirname(file), included)));
  return '#version 300 es\n' + expand(name);
}

test('shape shaders render synthetic and CPU-emitted quads', async ({ page }) => {
  type GpuFixture = {
    name: string;
    schemaVersion: number;
    emittedQuads: number;
    viewProj: number[];
    viewport: number[];
    fbSize: number[];
    fbOffset: number[];
    expectedPixelCoverage: 'empty' | 'nonempty';
    depthTest?: boolean;
    pixelChecks?: Array<{ x: number; y: number; rgba: number[]; tolerance: number }>;
    lifecycle?: { sequence: string; step: string; expectedTailBytes: number[] };
    layout: { stride: number; attributes: Array<{ location: number; type: 'FLOAT' | 'USHORT' | 'UBYTE'; count: number; normalized: boolean; offset: number }> };
    vertices: Array<{ bytes: number[] }>;
    indices: number[];
  };
  const executable = join(__dirname, '..', '..', 'build', 'tests', 'native-debug', `test_ui_shape_walk${process.platform === 'win32' ? '.exe' : ''}`);
  const output = execFileSync(executable, ['--gpu-fixtures'], { encoding: 'utf8' });
  const marker = 'UI_SHAPE_GPU_CASE ';
  const fixtures = output.split(/\r?\n/).filter(line => line.startsWith(marker)).map(line => JSON.parse(line.slice(marker.length)) as GpuFixture);
  expect(fixtures.map(fixture => fixture.name)).toEqual(expect.arrayContaining([
    'typed-horizontal-paint', 'typed-vertical-transparent-paint', 'radial-perspective',
    'screen-direct-viewport-offset', 'screen-scaled-viewport-offset', 'viewport-offset',
    'world-xy-singular', 'near-far-crossing', 'camera-crossing', 'beyond-far', 'behind-camera',
    'shadow-only-perspective', 'defaults', 'override', 'defaults-after-skip',
    'screen-direct-y-offset', 'screen-scaled-y-offset', 'screen-direct-y-shadow', 'screen-scaled-y-shadow',
    'depth-shadow-hierarchy', 'depth-shadow-zero-bias', 'depth-shadow-translucent', 'depth-shadow-strong-perspective',
    'depth-shadow-near-visible', 'depth-shadow-near-clipped', 'depth-shadow-far-visible', 'depth-shadow-far-clipped',
  ]));
  for (const fixture of fixtures) {
    expect(fixture.schemaVersion).toBe(2);
    expect(fixture.vertices.length % 4, fixture.name).toBe(0);
    expect(fixture.vertices.length > 0, fixture.name).toBe(fixture.emittedQuads > 0);
    for (const vertex of fixture.vertices) {
      expect(vertex.bytes, fixture.name).toHaveLength(fixture.layout.stride);
      if (fixture.lifecycle) expect(vertex.bytes.slice(20), `${fixture.name} copied tail`).toEqual(fixture.lifecycle.expectedTailBytes);
    }
    if (fixture.vertices.length > 0) {
      for (const index of fixture.indices) expect(index, `${fixture.name} index`).toBeLessThan(fixture.vertices.length);
    }
    if (fixture.depthTest) expect(fixture.pixelChecks?.length, `${fixture.name} depth oracle`).toBeGreaterThan(0);
  }
  await page.goto('about:blank');
  const result = await page.evaluate(({ vertex, fragments, fixtures }) => {
    const canvas = document.createElement('canvas');
    canvas.width = 96;
    canvas.height = 96;
    const gl = canvas.getContext('webgl2', { antialias: false, depth: true, preserveDrawingBuffer: true });
    if (!gl) throw new Error('WebGL2 unavailable');

    function compile(kind: number, source: string): WebGLShader {
      const shader = gl!.createShader(kind)!;
      gl!.shaderSource(shader, source);
      gl!.compileShader(shader);
      if (!gl!.getShaderParameter(shader, gl!.COMPILE_STATUS)) throw new Error(gl!.getShaderInfoLog(shader) || 'shader compile failed');
      return shader;
    }

    const vertexShader = compile(gl.VERTEX_SHADER, vertex);
    const programs: Record<string, WebGLProgram> = {};
    for (const [name, source] of Object.entries(fragments)) {
      const program = gl.createProgram()!;
      gl.attachShader(program, vertexShader);
      gl.attachShader(program, compile(gl.FRAGMENT_SHADER, source));
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(`${name}: ${gl.getProgramInfoLog(program) || 'shader link failed'}`);
      programs[name] = program;
    }

    gl.disable(gl.DITHER);
    const texture = gl.createTexture()!;
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.useProgram(programs.uber);
    gl.uniform1i(gl.getUniformLocation(programs.uber, 'u_texture'), 0);

    const globals = new Float32Array(64);
    globals[0] = globals[5] = globals[10] = globals[15] = 1;
    const uniformBuffer = gl.createBuffer()!;
    gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
    gl.bufferData(gl.UNIFORM_BUFFER, globals, gl.STATIC_DRAW);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, uniformBuffer);
    for (const program of Object.values(programs)) gl.uniformBlockBinding(program, gl.getUniformBlockIndex(program, 'Globals'), 0);

    const vao = gl.createVertexArray()!;
    gl.bindVertexArray(vao);
    const indices = gl.createBuffer()!;
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indices);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    const vertices = gl.createBuffer()!;
    gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
    const stride = 84;
    for (const [location, count, type, normalized, offset] of [
      [0, 3, gl.FLOAT, 0, 0], [3, 2, gl.UNSIGNED_SHORT, 1, 12], [2, 4, gl.UNSIGNED_BYTE, 1, 16],
      [4, 4, gl.FLOAT, 0, 20], [5, 4, gl.FLOAT, 0, 36], [6, 4, gl.FLOAT, 0, 52],
      [7, 1, gl.FLOAT, 0, 68], [8, 4, gl.UNSIGNED_BYTE, 1, 72], [9, 4, gl.UNSIGNED_BYTE, 1, 76],
      [10, 4, gl.UNSIGNED_BYTE, 0, 80],
    ]) {
      gl.enableVertexAttribArray(location);
      gl.vertexAttribPointer(location, count, type, normalized !== 0, stride, offset);
    }

    type ShapeCase = { name: string; mode: number; projective: boolean; padding: number; geometry: number[]; widths: number[]; color: number[]; size?: [number, number]; center?: [number, number]; warp?: number; affine?: [number, number, number, number]; };
    const cases: ShapeCase[] = [
      { name: 'box', mode: 1, projective: false, padding: 0, geometry: [12, 3, 16, 5], widths: [8, 2, 4, 10], color: [230, 30, 10, 255] },
      { name: 'box-projective', mode: 1, projective: true, padding: 0, geometry: [12, 3, 16, 5], widths: [8, 2, 4, 10], color: [230, 30, 10, 255] },
      { name: 'radial', mode: 2, projective: false, padding: 0, geometry: [0, Math.PI / 2, 0.25, 0], widths: [0, 0, 0, 0], color: [230, 30, 10, 255] },
      { name: 'radial-projective', mode: 2, projective: true, padding: 0, geometry: [0, Math.PI / 2, 0.25, 0], widths: [0, 0, 0, 0], color: [230, 30, 10, 255] },
      { name: 'radial-rect', mode: 2, projective: false, padding: 0, geometry: [0, 2 * Math.PI, 0, 0], widths: [0, 0, 0, 0], color: [230, 30, 10, 255], size: [70, 40] },
      { name: 'radial-rect-projective', mode: 2, projective: true, padding: 0, geometry: [0, 2 * Math.PI, 0, 0], widths: [0, 0, 0, 0], color: [230, 30, 10, 255], size: [70, 40] },
      { name: 'radial-90-center', mode: 2, projective: false, padding: 0, geometry: [0, Math.PI / 2, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'radial-90-center-projective', mode: 2, projective: true, padding: 0, geometry: [0, Math.PI / 2, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'radial-180-center', mode: 2, projective: false, padding: 0, geometry: [0, Math.PI, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'radial-180-center-projective', mode: 2, projective: true, padding: 0, geometry: [0, Math.PI, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'radial-270-center', mode: 2, projective: false, padding: 0, geometry: [0, 3 * Math.PI / 2, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'radial-270-center-projective', mode: 2, projective: true, padding: 0, geometry: [0, 3 * Math.PI / 2, 0, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5] },
      { name: 'shadow', mode: 3, projective: false, padding: 8, geometry: [8, 3, 12, 5], widths: [2, 5, 8, 0], color: [70, 100, 220, 255] },
      { name: 'shadow-projective', mode: 3, projective: true, padding: 0, geometry: [8, 3, 12, 5], widths: [2, 5, 8, 0], color: [70, 100, 220, 255] },
    ];
    const coords: Record<string, Array<[number, number]>> = {
      box: [[48, 48], [18, 48], [78, 48], [48, 79], [48, 19], [18, 78], [78, 78], [78, 18], [18, 18]],
      'box-projective': [[48, 48], [18, 48], [15, 78]],
      radial: [[64, 32], [64, 64], [32, 32], [48, 48]],
      'radial-projective': [[64, 32], [64, 64], [48, 48]],
      'radial-rect': [[48, 63]],
      'radial-rect-projective': [[48, 63]],
      'radial-90-center': [[48, 48]],
      'radial-90-center-projective': [[48, 48]],
      'radial-180-center': [[48, 48]],
      'radial-180-center-projective': [[48, 48]],
      'radial-270-center': [[48, 48]],
      'radial-270-center-projective': [[48, 48]],
      shadow: [[48, 48], [13, 48], [8, 48]],
      'shadow-projective': [[48, 48]],
    };
    const tau = 2 * Math.PI;
    const variants = [
      { name: 'screen', projective: false },
      { name: 'affine', projective: false, affine: [0.9, 0.3, -0.2, 1.1] as [number, number, number, number] },
      { name: 'world-affine', projective: true, warp: 0 },
      { name: 'perspective', projective: true, warp: 0.45 },
    ];
    const radialCases: ShapeCase[] = [];
    for (const variant of variants) {
      for (const [label, end, inner] of [
        ['empty', 0, 0], ['full', tau, 0],
        ['tiny-0.005', 0.005, 0], ['tiny-0.0005', 0.0005, 0], ['tiny-0.00005', 0.00005, 0],
        ['almost-full-0.005', tau - 0.005, 0], ['almost-full-0.0005', tau - 0.0005, 0], ['almost-full-0.00005', tau - 0.00005, 0],
        ['ring-0.99', tau, 0.99], ['ring-0.999', tau, 0.999], ['ring-0.9999', tau, 0.9999],
      ] as Array<[string, number, number]>) {
        const shape: ShapeCase = {
          ...variant, name: `${variant.name}:${label}`, mode: 2, padding: 2,
          geometry: [0, end, inner, 0], widths: [0, 0, 0, 0], color: [255, 255, 255, 255], center: [48.5, 48.5],
        };
        cases.push(shape);
        radialCases.push(shape);
        coords[shape.name] = [];
      }
    }

    function sourcePoint(shape: ShapeCase, x: number, y: number): [number, number] {
      const [cx, cy] = shape.center ?? [48, 48];
      const [a, b, c, d] = shape.affine ?? [1, 0, 0, 1];
      return [cx + a * (x - cx) + c * (y - cy), cy + b * (x - cx) + d * (y - cy)];
    }

    function projectedPoint(shape: ShapeCase, x: number, y: number): [number, number] {
      const [sx, sy] = sourcePoint(shape, x, y);
      const nx = sx / 48 - 1, ny = sy / 48 - 1;
      const w = 1 + (shape.projective ? shape.warp ?? 0.08 : 0) * nx;
      return [48 * (nx / w + 1), 48 * (ny / w + 1)];
    }

    function localPoint(shape: ShapeCase, x: number, y: number): [number, number] {
      const nx = x / 48 - 1, ny = y / 48 - 1;
      const denominator = 1 - (shape.projective ? shape.warp ?? 0.08 : 0) * nx;
      const [cx, cy] = shape.center ?? [48, 48];
      const sx = 48 * (nx / denominator + 1) - cx, sy = 48 * (ny / denominator + 1) - cy;
      const [a, b, c, d] = shape.affine ?? [1, 0, 0, 1];
      return [(d * sx - c * sy) / (a * d - b * c), (-b * sx + a * sy) / (a * d - b * c)];
    }

    // A projected polygon's area is independent of the shaders' derivative AA.
    function radialArea(shape: ShapeCase): number {
      const [cx, cy] = shape.center ?? [48, 48];
      const sweep = shape.geometry[1];
      const count = Math.max(2, Math.ceil(sweep * 1024));
      const sectorArea = (radius: number): number => {
        const points = [projectedPoint(shape, cx, cy)];
        for (let i = 0; i <= count; i++) {
          const angle = sweep * i / count;
          points.push(projectedPoint(shape, cx + radius * Math.cos(angle), cy - radius * Math.sin(angle)));
        }
        let area = 0;
        for (let i = 0; i < points.length; i++) {
          const a = points[i], b = points[(i + 1) % points.length];
          area += a[0] * b[1] - b[0] * a[1];
        }
        return Math.abs(area) / 2;
      };
      return sectorArea(32) - sectorArea(32 * shape.geometry[2]);
    }

    function framebuffer(): Uint8Array {
      const pixels = new Uint8Array(canvas.width * canvas.height * 4);
      gl!.readPixels(0, 0, canvas.width, canvas.height, gl!.RGBA, gl!.UNSIGNED_BYTE, pixels);
      return pixels;
    }

    function pixelAt(pixels: Uint8Array, x: number, y: number): number[] {
      return Array.from(pixels.subarray(4 * (y * canvas.width + x), 4 * (y * canvas.width + x + 1)));
    }

    function difference(first: Uint8Array, second: Uint8Array): number {
      if (first.length !== second.length) throw new Error('framebuffer dimensions differ');
      let differingBytes = 0;
      for (let i = 0; i < first.length; i++) if (first[i] !== second[i]) differingBytes++;
      return differingBytes;
    }

    const samples: Record<string, number[][]> = {};
    const parity: Record<string, number> = {};
    const buffers = new Map<string, Uint8Array>();
    const radial: Record<string, { alphaArea: number; expectedArea: number; oppositeMax: number; missingArea: number; oppositeDeficit: number }> = {};
    // Compare the entire target, including AA fringes and discarded fragments.
    for (const shape of cases) {
      for (const fragment of ['shape', 'uber']) {
        gl.useProgram(programs[fragment]);
        globals[3] = shape.projective ? shape.warp ?? 0.08 : 0;
        gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
        gl.bufferSubData(gl.UNIFORM_BUFFER, 0, globals);
        const data = new DataView(new ArrayBuffer(4 * stride));
        for (let i = 0; i < 4; i++) {
          const [width, height] = shape.size ?? [64, 64];
          const [centerX, centerY] = shape.center ?? [48, 48];
          const padding = shape.projective ? (shape.mode === 3 ? shape.widths[2] : 0) : shape.padding;
          const [x, y] = sourcePoint(shape,
            centerX + (i === 1 || i === 2 ? width / 2 + padding : -width / 2 - padding),
            centerY + (i >= 2 ? -height / 2 - padding : height / 2 + padding));
          const base = i * stride;
          data.setFloat32(base, x / 48 - 1, true);
          data.setFloat32(base + 4, y / 48 - 1, true);
          data.setFloat32(base + 8, 0, true);
          for (let c = 0; c < 4; c++) data.setUint8(base + 16 + c, shape.color[c]);
          data.setFloat32(base + 20, width, true);
          data.setFloat32(base + 24, height, true);
          data.setFloat32(base + 28, shape.projective ? 1.1 : shape.padding, true);
          data.setFloat32(base + 32, shape.projective ? centerX / 48 - 1 : 0, true);
          for (let c = 0; c < 4; c++) data.setFloat32(base + 36 + c * 4, shape.geometry[c], true);
          for (let c = 0; c < 4; c++) data.setFloat32(base + 52 + c * 4, shape.widths[c], true);
          data.setFloat32(base + 68, shape.projective ? centerY / 48 - 1 : 0, true);
          for (let c = 0; c < 4; c++) data.setUint8(base + 72 + c, shape.color[c]);
          data.setUint8(base + 76, 10); data.setUint8(base + 77, 210); data.setUint8(base + 78, 30); data.setUint8(base + 79, 255);
          data.setUint8(base + 80, shape.mode === 3 ? 160 : 255);
          data.setUint8(base + 81, shape.mode);
          data.setUint8(base + 82, 0);
          data.setUint8(base + 83, shape.projective ? 2 : 0);
        }
        gl.bufferData(gl.ARRAY_BUFFER, data.buffer, gl.STREAM_DRAW);
        gl.viewport(0, 0, 96, 96);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
        const pixels = framebuffer();
        if (fragment === 'shape') {
          buffers.set(shape.name, pixels);
          samples[shape.name] = coords[shape.name].map(([x, y]) => pixelAt(pixels, x, y));
        } else parity[shape.name] = difference(buffers.get(shape.name)!, pixels);
      }
    }
    for (const shape of radialCases) {
      const pixels = buffers.get(shape.name)!;
      const full = buffers.get(`${shape.name.split(':')[0]}:full`)!;
      let alphaArea = 0, oppositeMax = 0, missingArea = 0, oppositeDeficit = 0;
      for (let y = 0; y < 96; y++) {
        for (let x = 0; x < 96; x++) {
          const alpha = pixels[4 * (y * 96 + x) + 3], fullAlpha = full[4 * (y * 96 + x) + 3];
          alphaArea += alpha / 255;
          missingArea += (fullAlpha - alpha) / 255;
          const [lx, ly] = localPoint(shape, x + 0.5, y + 0.5);
          if (lx < -8 && lx > -24 && Math.abs(ly) < 2) {
            oppositeMax = Math.max(oppositeMax, alpha);
            oppositeDeficit = Math.max(oppositeDeficit, fullAlpha - alpha);
          }
        }
      }
      radial[shape.name] = { alphaArea, expectedArea: radialArea(shape), oppositeMax, missingArea, oppositeDeficit };
    }

    const fixtureCoords: Record<string, Array<[number, number]>> = {
      'typed-horizontal-paint': [[30, 570], [170, 570]],
      'typed-vertical-transparent-paint': [[100, 580], [100, 550]],
      'radial-perspective': [[500, 300], [400, 300]],
      'near-far-crossing': [[215, 300], [250, 300], [400, 300], [550, 300], [585, 300]],
      'camera-crossing': [[200, 300], [400, 300], [560, 300], [650, 300]],
      'viewport-offset': [[421, 287], [201, 287]],
      'world-xy-singular': [[400, 300], [100, 300]],
      'shadow-only-perspective': [[267, 300], [650, 300]],
      'screen-direct-viewport-offset': [[125, 575], [75, 575]],
      'screen-scaled-viewport-offset': [[50, 1150], [150, 1150]],
      'screen-direct-y-offset': [[151, 64], [151, 84]],
      'screen-direct-y-shadow': [[151, 64], [151, 84]],
      'screen-scaled-y-offset': [[101, 37], [101, 70]],
      'screen-scaled-y-shadow': [[101, 37], [101, 70]],
      defaults: [[400, 300], [100, 300]], override: [[400, 300], [100, 300]],
      'defaults-after-skip': [[400, 300], [100, 300]],
    };
    const fixtureCoverage: Record<string, { nonzeroPixels: number; outsideViewport: number }> = {};
    const checkedPixels: Array<{ name: string; actual: number[]; expected: number[]; tolerance: number }> = [];
    let lifecycleBaseline: Uint8Array | undefined;
    const lifecycleDifferences: Record<string, number> = {};
    for (const fixture of fixtures) {
      const scaled = fixture.fbSize[0] > 0;
      const viewport = scaled
        ? [Math.round(fixture.fbOffset[0]), Math.round(fixture.fbOffset[1]),
          Math.trunc(fixture.fbSize[0]) - 2 * Math.round(fixture.fbOffset[0]), Math.trunc(fixture.fbSize[1]) - 2 * Math.round(fixture.fbOffset[1])]
        : fixture.viewport.map(Math.trunc);
      canvas.width = scaled ? Math.trunc(fixture.fbSize[0]) : viewport[0] + viewport[2];
      canvas.height = scaled ? Math.trunc(fixture.fbSize[1]) : viewport[1] + viewport[3];
      globals.fill(0);
      globals.set(fixture.viewProj);
      gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, globals);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(fixture.indices), gl.STREAM_DRAW);
      gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(fixture.vertices.flatMap(vertex => vertex.bytes)), gl.STREAM_DRAW);
      for (const attr of fixture.layout.attributes) {
        const type = { FLOAT: gl.FLOAT, USHORT: gl.UNSIGNED_SHORT, UBYTE: gl.UNSIGNED_BYTE }[attr.type];
        gl.vertexAttribPointer(attr.location, attr.count, type, attr.normalized, fixture.layout.stride, attr.offset);
      }
      gl.viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.depthMask(true);
      gl.depthFunc(gl.LESS);
      if (fixture.depthTest) gl.enable(gl.DEPTH_TEST); else gl.disable(gl.DEPTH_TEST);
      const containsSprite = fixture.vertices.some(vertex => vertex.bytes[81] === 0);
      let first: Uint8Array | undefined;
      for (const fragment of containsSprite ? ['uber'] : ['shape', 'uber']) {
        gl.useProgram(programs[fragment]);
        gl.clearDepth(1);
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        if (fixture.vertices.length > 0) gl.drawElements(gl.TRIANGLES, fixture.indices.length, gl.UNSIGNED_SHORT, 0);
        const pixels = framebuffer();
        if (first) parity[fixture.name] = difference(first, pixels);
        else {
          first = pixels;
          samples[fixture.name] = (fixtureCoords[fixture.name] ?? []).map(([x, y]) => pixelAt(pixels, x, y));
          let nonzeroPixels = 0, outsideViewport = 0;
          for (let y = 0; y < canvas.height; y++) {
            for (let x = 0; x < canvas.width; x++) {
              if (pixels[4 * (y * canvas.width + x) + 3] === 0) continue;
              nonzeroPixels++;
              if (x < viewport[0] || y < viewport[1] || x >= viewport[0] + viewport[2] || y >= viewport[1] + viewport[3]) outsideViewport++;
            }
          }
          fixtureCoverage[fixture.name] = { nonzeroPixels, outsideViewport };
          for (const check of fixture.pixelChecks ?? []) checkedPixels.push({ name: fixture.name, actual: pixelAt(pixels, check.x, check.y), expected: check.rgba, tolerance: check.tolerance });
          if (fixture.lifecycle) {
            if (!lifecycleBaseline) lifecycleBaseline = pixels;
            lifecycleDifferences[fixture.lifecycle.step] = difference(lifecycleBaseline, pixels);
          }
        }
      }
    }
    return { samples, parity, radial, fixtureCoverage, checkedPixels, lifecycleDifferences, error: gl.getError() };
  }, {
    vertex: shaderSource('sprite_ui_shape.vert'),
    fragments: {
      shape: shaderSource('ui_shape.frag'),
      uber: shaderSource('ui_shape_uber.frag'),
    },
    fixtures,
  });

  expect(result.error).toBe(0);
  for (const [name, difference] of Object.entries(result.parity)) expect(difference, `${name} full framebuffer parity`).toBe(0);
  for (const fixture of fixtures) {
    const coverage = result.fixtureCoverage[fixture.name];
    expect(coverage.outsideViewport, `${fixture.name} viewport`).toBe(0);
    if (fixture.expectedPixelCoverage === 'empty') expect(coverage.nonzeroPixels, fixture.name).toBe(0);
    else expect(coverage.nonzeroPixels, fixture.name).toBeGreaterThan(20);
  }
  for (const check of result.checkedPixels) {
    const label = `${check.name}: actual ${JSON.stringify(check.actual)}, expected ${JSON.stringify(check.expected)}`;
    expect(check.actual, label).toHaveLength(4);
    for (let channel = 0; channel < 4; channel++) expect(Math.abs(check.actual[channel] - check.expected[channel]), `${label} channel ${channel}`).toBeLessThanOrEqual(check.tolerance);
  }
  for (const [step, difference] of Object.entries(result.lifecycleDifferences)) expect(difference, `${step} mode 0 pixels`).toBe(0);
  for (const name of ['defaults', 'override', 'defaults-after-skip']) {
    expect(result.samples[name][0], `${name} white texture and premultiplied tint`).toEqual([34, 68, 102, 128]);
    expect(result.samples[name][1][3], `${name} outside sprite`).toBe(0);
  }
  const [fill, left, right, top, bottom, topLeft, topRight, bottomRight, bottomLeft] = result.samples.box;
  expect(fill[0]).toBeGreaterThan(200);
  for (const border of [left, right, top, bottom, topRight, bottomLeft]) expect(border[1]).toBeGreaterThan(150);
  for (const outside of [topLeft, bottomRight]) expect(outside[3]).toBeLessThan(20);
  expect(result.samples['box-projective'][0][0]).toBeGreaterThan(200);
  expect(result.samples['box-projective'][1][1]).toBeGreaterThan(150);
  expect(result.samples['box-projective'][2][3]).toBeLessThan(20);
  for (const name of ['radial', 'radial-projective']) {
    expect(result.samples[name][0][3]).toBeGreaterThan(200);
    expect(result.samples[name][1][3]).toBeLessThan(20);
    expect(result.samples[name][2][3]).toBeLessThan(20);
  }
  expect(result.samples.radial[3][3]).toBeLessThan(20);
  for (const suffix of ['', '-projective']) {
    expect(result.samples[`radial-90-center${suffix}`][0][3]).toBeGreaterThan(45);
    expect(result.samples[`radial-90-center${suffix}`][0][3]).toBeLessThan(85);
    expect(result.samples[`radial-180-center${suffix}`][0][3]).toBeGreaterThan(110);
    expect(result.samples[`radial-180-center${suffix}`][0][3]).toBeLessThan(145);
    expect(result.samples[`radial-270-center${suffix}`][0][3]).toBeGreaterThan(170);
    expect(result.samples[`radial-270-center${suffix}`][0][3]).toBeLessThan(210);
  }
  for (const name of ['radial-rect', 'radial-rect-projective']) expect(result.samples[name][0][3]).toBeGreaterThan(220);
  expect(result.samples.shadow[0][3]).toBeGreaterThan(80);
  expect(result.samples.shadow[1][3]).toBeGreaterThan(10);
  expect(result.samples.shadow[2][3]).toBeLessThan(10);
  expect(result.samples['shadow-projective'][0][3]).toBeGreaterThan(80);
  expect(result.samples['typed-horizontal-paint'][0][3]).toBeGreaterThan(20);
  expect(result.samples['typed-horizontal-paint'][0][0]).toBeGreaterThan(result.samples['typed-horizontal-paint'][1][0]);
  expect(result.samples['typed-horizontal-paint'][1][1]).toBeGreaterThan(result.samples['typed-horizontal-paint'][0][1]);
  expect(result.samples['radial-perspective'][0][3]).toBeGreaterThan(50);
  expect(result.samples['radial-perspective'][1][3]).toBeLessThan(20);
  const [opaqueTop, transparentBottom] = result.samples['typed-vertical-transparent-paint'];
  expect(opaqueTop[3]).toBeGreaterThan(transparentBottom[3] + 20);
  for (const pixel of [opaqueTop, transparentBottom]) expect(Math.abs(pixel[0] - pixel[3])).toBeLessThanOrEqual(1);
  for (const name of ['viewport-offset', 'world-xy-singular', 'screen-direct-viewport-offset', 'screen-scaled-viewport-offset',
    'screen-direct-y-offset', 'screen-scaled-y-offset', 'screen-direct-y-shadow', 'screen-scaled-y-shadow']) {
    expect(result.samples[name][0][3], `${name} interior`).toBeGreaterThan(220);
    expect(result.samples[name][1][3], `${name} outside`).toBe(0);
  }
  for (const index of [0, 4]) expect(result.samples['near-far-crossing'][index][3], 'clipped depth').toBe(0);
  for (const index of [1, 2, 3]) expect(result.samples['near-far-crossing'][index][3], 'unclipped depth').toBeGreaterThan(220);
  for (const index of [0, 1, 2]) expect(result.samples['camera-crossing'][index][3], 'positive camera sheet').toBeGreaterThan(220);
  expect(result.samples['camera-crossing'][3][3], 'outside camera-crossing support').toBe(0);
  expect(result.samples['shadow-only-perspective'][0][3]).toBeGreaterThan(100);
  expect(result.samples['shadow-only-perspective'][1][3]).toBe(0);

  for (const variant of ['screen', 'affine', 'world-affine', 'perspective']) {
    const full = result.radial[`${variant}:full`];
    expect(Math.abs(full.alphaArea - full.expectedArea), `${variant} disk area`).toBeLessThan(12);
    expect(result.radial[`${variant}:empty`].alphaArea, `${variant} zero sweep`).toBe(0);
    let previousSector = Infinity, previousMissing = Infinity, previousRing = Infinity;
    for (const sweep of ['0.005', '0.0005', '0.00005']) {
      const tiny = result.radial[`${variant}:tiny-${sweep}`];
      expect(tiny.alphaArea, `${variant} sweep ${sweep} finite area`).toBeLessThan(tiny.expectedArea * 1.6 + 1);
      expect(tiny.alphaArea, `${variant} shrinking sweep`).toBeLessThan(previousSector);
      expect(tiny.oppositeMax, `${variant} opposite-ray ghost`).toBe(0);
      previousSector = tiny.alphaArea;
      const almost = result.radial[`${variant}:almost-full-${sweep}`];
      const expectedMissing = full.expectedArea - almost.expectedArea;
      expect(Math.abs(almost.missingArea - expectedMissing), `${variant} near-TAU missing area`).toBeLessThan(expectedMissing * 0.6 + 1);
      expect(almost.missingArea, `${variant} shrinking gap`).toBeLessThanOrEqual(previousMissing);
      expect(almost.oppositeDeficit, `${variant} opposite-ray gap`).toBe(0);
      previousMissing = almost.missingArea;
    }
    for (const inner of ['0.99', '0.999', '0.9999']) {
      const ring = result.radial[`${variant}:ring-${inner}`];
      expect(ring.alphaArea, `${variant} ring ${inner} finite area`).toBeLessThan(ring.expectedArea * 1.6 + 1);
      expect(ring.alphaArea, `${variant} shrinking ring`).toBeLessThan(previousRing);
      previousRing = ring.alphaArea;
    }
  }
});
