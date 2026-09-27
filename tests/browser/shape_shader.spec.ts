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
    layout: { stride: number; attributes: Array<{ location: number; type: 'FLOAT' | 'USHORT' | 'UBYTE'; count: number; normalized: boolean; offset: number }> };
    vertices: Array<{ bytes: number[] }>;
    indices: number[];
  };
  const executable = join(__dirname, '..', '..', 'build', 'tests', 'native-debug', `test_ui_shape_walk${process.platform === 'win32' ? '.exe' : ''}`);
  const output = execFileSync(executable, ['--gpu-fixtures'], { encoding: 'utf8' });
  const marker = 'UI_SHAPE_GPU_CASE ';
  const fixtures = output.split(/\r?\n/).filter(line => line.startsWith(marker)).map(line => JSON.parse(line.slice(marker.length)) as GpuFixture)
    .filter(fixture => ['typed-horizontal-paint', 'radial-perspective'].includes(fixture.name));
  expect(fixtures.map(fixture => fixture.name)).toEqual(['typed-horizontal-paint', 'radial-perspective']);
  for (const fixture of fixtures) {
    expect(fixture.schemaVersion).toBe(2);
    expect(fixture.emittedQuads).toBe(1);
    expect(fixture.vertices).toHaveLength(4);
  }
  await page.goto('about:blank');
  const result = await page.evaluate(({ vertex, fragments, fixtures }) => {
    const canvas = document.createElement('canvas');
    canvas.width = 800;
    canvas.height = 600;
    const gl = canvas.getContext('webgl2', { antialias: false, preserveDrawingBuffer: true });
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

    type ShapeCase = { name: string; mode: number; projective: boolean; padding: number; geometry: number[]; widths: number[]; color: number[]; size?: [number, number]; center?: [number, number]; };
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
    const samples: Record<string, number[][]> = {};
    for (const shape of cases) {
      const fragment = shape.mode === 1 ? 'box' : shape.mode === 2 ? 'radial' : 'shadow';
      gl.useProgram(programs[fragment]);
      globals[3] = shape.projective ? 0.08 : 0; // W varies across projective quads.
      gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, globals);
      const data = new DataView(new ArrayBuffer(4 * stride));
      for (let i = 0; i < 4; i++) {
        const [width, height] = shape.size ?? [64, 64];
        const [centerX, centerY] = shape.center ?? [48, 48];
        const x = centerX + (i === 1 || i === 2 ? width / 2 + shape.padding : -width / 2 - shape.padding);
        const y = centerY + (i >= 2 ? -height / 2 - shape.padding : height / 2 + shape.padding);
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
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
      const pixel = new Uint8Array(4);
      samples[shape.name] = coords[shape.name].map(([x, y]) => {
        gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        return Array.from(pixel);
      });
    }
    const fixtureCoords: Record<string, Array<[number, number]>> = {
      'typed-horizontal-paint': [[30, 570], [170, 570]],
      'radial-perspective': [[500, 300], [400, 300]],
    };
    for (const fixture of fixtures) {
      globals.set(fixture.viewProj);
      gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, globals);
      const mode = fixture.vertices[0].bytes[81];
      gl.useProgram(programs[mode === 1 ? 'box' : 'radial']);
      gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(fixture.indices), gl.STREAM_DRAW);
      gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(fixture.vertices.flatMap(vertex => vertex.bytes)), gl.STREAM_DRAW);
      for (const attr of fixture.layout.attributes) {
        const type = { FLOAT: gl.FLOAT, USHORT: gl.UNSIGNED_SHORT, UBYTE: gl.UNSIGNED_BYTE }[attr.type];
        gl.vertexAttribPointer(attr.location, attr.count, type, attr.normalized, fixture.layout.stride, attr.offset);
      }
      gl.viewport(0, 0, fixture.viewport[2], fixture.viewport[3]);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.drawElements(gl.TRIANGLES, fixture.indices.length, gl.UNSIGNED_SHORT, 0);
      const pixel = new Uint8Array(4);
      samples[fixture.name] = fixtureCoords[fixture.name].map(([x, y]) => {
        gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
        return Array.from(pixel);
      });
    }
    return { samples, error: gl.getError() };
  }, {
    vertex: shaderSource('sprite_ui_shape.vert'),
    fragments: {
      box: shaderSource('ui_shape.frag'),
      radial: shaderSource('ui_shape_radial.frag'),
      shadow: shaderSource('ui_shape_shadow.frag'),
      uber: shaderSource('ui_shape_uber.frag'),
    },
    fixtures,
  });

  expect(result.error).toBe(0);
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
});
