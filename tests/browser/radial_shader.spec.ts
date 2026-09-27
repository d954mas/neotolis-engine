import { expect, test } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const shaderDir = join(__dirname, '..', '..', 'assets', 'shaders');

function shaderSource(name: string): string {
  const expand = (file: string): string => readFileSync(join(shaderDir, file), 'utf8')
    .replace(/^#pragma once\s*$/gm, '')
    .replace(/^#include "([^"]+)"\s*$/gm, (_line, included: string) => expand(included));
  return '#version 300 es\n' + expand(name);
}

test('radial image uses source coordinates through atlas D4 and explicit flips', async ({ page }) => {
  await page.goto('about:blank');
  const result = await page.evaluate(({ vertex, fragment }) => {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 64;
    const gl = canvas.getContext('webgl2', { antialias: false, preserveDrawingBuffer: true });
    if (!gl) throw new Error('WebGL2 unavailable');

    function compile(kind: number, source: string): WebGLShader {
      const shader = gl!.createShader(kind)!;
      gl!.shaderSource(shader, source);
      gl!.compileShader(shader);
      if (!gl!.getShaderParameter(shader, gl!.COMPILE_STATUS)) throw new Error(gl!.getShaderInfoLog(shader) || 'shader compile failed');
      return shader;
    }

    const program = gl.createProgram()!;
    gl.attachShader(program, compile(gl.VERTEX_SHADER, vertex));
    gl.attachShader(program, compile(gl.FRAGMENT_SHADER, fragment));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program) || 'shader link failed');
    gl.useProgram(program);

    const globals = new Float32Array(64);
    globals[0] = globals[5] = globals[10] = globals[15] = 1;
    const uniformBuffer = gl.createBuffer()!;
    gl.bindBuffer(gl.UNIFORM_BUFFER, uniformBuffer);
    gl.bufferData(gl.UNIFORM_BUFFER, globals, gl.STATIC_DRAW);
    gl.uniformBlockBinding(program, gl.getUniformBlockIndex(program, 'Globals'), 0);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, uniformBuffer);
    gl.uniform4f(gl.getUniformLocation(program, 'u_reveal_mode'), 2, 0, 0, 0); // HIDE
    gl.uniform1i(gl.getUniformLocation(program, 'u_texture'), 0);

    const texture = gl.createTexture()!;
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);

    const vertices = gl.createBuffer()!;
    const indices = gl.createBuffer()!;
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indices);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0, 1, 2, 0, 2, 3]), gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
    const stride = 64;
    for (const [location, count, type, normalized, offset] of [
      [0, 3, gl.FLOAT, 0, 0], [2, 4, gl.UNSIGNED_BYTE, 1, 16], [3, 2, gl.UNSIGNED_SHORT, 1, 12],
      [4, 4, gl.FLOAT, 0, 20], [5, 4, gl.FLOAT, 0, 36], [7, 1, gl.FLOAT, 0, 52], [6, 2, gl.FLOAT, 0, 56],
    ]) {
      gl.enableVertexAttribArray(location);
      gl.vertexAttribPointer(location, count, type, normalized !== 0, stride, offset);
    }

    const failures: string[] = [];
    const trimLeft = 0.2;
    const corners: Array<[number, number]> = [[trimLeft, 0], [1, 0], [1, 1], [trimLeft, 1]];
    const pixel = new Uint8Array(4);
    let lastData: ArrayBuffer | undefined;
    function alpha(x: number, y: number): number {
      gl!.readPixels(x, y, 1, 1, gl!.RGBA, gl!.UNSIGNED_BYTE, pixel);
      return pixel[3];
    }

    for (let d4 = 0; d4 < 8; d4++) {
      for (let flip = 0; flip < 4; flip++) {
        const data = new DataView(new ArrayBuffer(4 * stride));
        for (let i = 0; i < corners.length; i++) {
          const [sourceX, sourceY] = corners[i];
          const base = i * stride;
          let atlasX = (sourceX - trimLeft) / (1 - trimLeft);
          let atlasY = sourceY;
          if (d4 & 4) [atlasX, atlasY] = [atlasY, atlasX];
          if (d4 & 1) atlasX = 1 - atlasX;
          if (d4 & 2) atlasY = 1 - atlasY;
          const x = (2 * sourceX - 1) * (flip & 1 ? -1 : 1);
          const y = (1 - 2 * sourceY) * (flip & 2 ? -1 : 1);
          data.setFloat32(base, x, true);
          data.setFloat32(base + 4, y, true);
          data.setFloat32(base + 8, 0, true);
          data.setUint16(base + 12, Math.round((0.2 + 0.3 * atlasX) * 65535), true);
          data.setUint16(base + 14, Math.round((0.3 + 0.4 * atlasY) * 65535), true);
          for (let c = 0; c < 4; c++) data.setUint8(base + 16 + c, 255);
          data.setFloat32(base + 20, 0, true);
          data.setFloat32(base + 24, Math.PI / 2, true);
          data.setFloat32(base + 28, 0, true);
          data.setFloat32(base + 32, 0, true);
          data.setFloat32(base + 36, 1, true);
          data.setFloat32(base + 40, 1, true);
          data.setFloat32(base + 44, 1, true);
          data.setFloat32(base + 48, 0, true);
          data.setFloat32(base + 52, 1, true);
          data.setFloat32(base + 56, sourceX, true);
          data.setFloat32(base + 60, sourceY, true);
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, vertices);
        gl.bufferData(gl.ARRAY_BUFFER, data.buffer, gl.STREAM_DRAW);
        lastData = data.buffer;
        gl.viewport(0, 0, 64, 64);
        gl.clearColor(0, 0, 0, 0);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
        const litX = flip & 1 ? 28 : 35;
        const litY = flip & 2 ? 47 : 16;
        const hiddenX = flip & 1 ? 34 : 29;
        if (alpha(litX, litY) < 220 || alpha(hiddenX, litY) > 20) {
          failures.push(`d4=${d4} flip=${flip}: lit=${alpha(litX, litY)} hidden=${alpha(hiddenX, litY)}`);
        }
      }
    }
    const empty = new DataView(lastData!);
    for (let i = 0; i < 4; i++) empty.setFloat32(i * stride + 24, 0, true);
    gl.bufferData(gl.ARRAY_BUFFER, empty.buffer, gl.STREAM_DRAW);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);
    const center = new Uint8Array(5 * 5 * 4);
    gl.readPixels(30, 30, 5, 5, gl.RGBA, gl.UNSIGNED_BYTE, center);
    if (center.some((value, index) => index % 4 === 3 && value !== 0)) failures.push('zero sweep left visible pixels');
    return { failures, error: gl.getError() };
  }, { vertex: shaderSource('sprite_radial.vert'), fragment: shaderSource('radial_image.frag') });

  expect(result.error).toBe(0);
  expect(result.failures).toEqual([]);
});
