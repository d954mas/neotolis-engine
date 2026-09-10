import { defineConfig } from '@playwright/test';
import { resolve } from 'node:path';

const preset = process.env.NT_BASIS_PRESET;
const directory = process.env.NT_SHOWCASE_DIR;
if (!preset || !directory) throw new Error('Set NT_BASIS_PRESET and NT_SHOWCASE_DIR to the exact Basis fixture build.');
const port = Number(process.env.NT_SHOWCASE_PORT || 8453);
const hardware = process.env.NT_BASIS_HARDWARE === '1';

export default defineConfig({
  testDir: '.',
  testMatch: 'basis.spec.ts',
  workers: 1,
  retries: 0,
  use: {
    baseURL: `http://localhost:${port}`,
    channel: hardware ? 'chromium' : undefined,
    launchOptions: { args: hardware ? ['--enable-gpu'] : ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] },
  },
  webServer: {
    command: 'node serve.mjs',
    port,
    env: { NT_SHOWCASE_DIR: resolve(directory), NT_SHOWCASE_PORT: String(port) },
    reuseExistingServer: false,
  },
});
