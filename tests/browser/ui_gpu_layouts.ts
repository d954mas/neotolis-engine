import { execFileSync } from 'node:child_process';
import { join } from 'node:path';

// [location, count, nt_vertex_type_t, normalized (0/1), byte offset], as the engine declares them.
export type GpuAttr = [number, number, number, number, number];
export type GpuLayout = { name: string; stride: number; attrs: GpuAttr[] };

const executable = join(__dirname, '..', '..', 'build', 'tests', 'native-debug', `test_ui_shape_walk${process.platform === 'win32' ? '.exe' : ''}`);

// The native walker test prints the engine's own layouts and CPU-emitted instances; specs never restate them.
export function uiGpuOutput(): string {
  return execFileSync(executable, ['--gpu-fixtures'], { encoding: 'utf8' });
}

export function uiGpuLayout(output: string, name: string): GpuLayout {
  const marker = 'UI_GPU_LAYOUT ';
  const layout = output.split(/\r?\n/).filter(line => line.startsWith(marker)).map(line => JSON.parse(line.slice(marker.length)) as GpuLayout).find(entry => entry.name === name);
  if (!layout) throw new Error(`test_ui_shape_walk --gpu-fixtures printed no ${name} layout`);
  return layout;
}
