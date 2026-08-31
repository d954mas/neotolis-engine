import { defineConfig, devices } from '@playwright/test';
import { join } from 'node:path';

// Two static servers, one per wasm target: the browser_smoke app (input/rich/context_loss specs)
// and the devapi_host capture build (devapi.spec.ts). Each spec file runs in the project whose
// server serves the app it drives — a plain `npx playwright test` covers both, provided both
// targets are built (browser_smoke via the wasm-debug preset; devapi_host via
// `cmake --build build/_cmake/wasm-debug --target devapi_host`).
const PORT = Number(process.env.NT_SHOWCASE_PORT || 8123);
const DEVAPI_PORT = Number(process.env.NT_DEVAPI_PORT || 8124);
const DEVAPI_DIR = process.env.NT_DEVAPI_DIR || join(__dirname, '..', '..', 'build', 'examples', 'devapi_host', 'wasm-debug');

// SwiftShader GL: WebGL2 must work in headless CI (and on GPUs whose headless context is
// unreliable). The engine needs a real GL2 context for texture upload; without this the context
// is lost and the wasm app traps on first glTexImage2D.
const chromiumGl = {
  args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
};

export default defineConfig({
  testDir: '.',
  // CI starts a fresh runner; locally a left-over server is reused. Single retry absorbs a cold-boot flake.
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? 'github' : 'list',
  use: {
    baseURL: `http://localhost:${PORT}`,
    // Clipboard paste is the path under test -- grant read/write so navigator.clipboard.writeText works.
    permissions: ['clipboard-read', 'clipboard-write'],
    trace: 'on-first-retry',
  },
  // Port checks only (not url): each CI job builds just the wasm target its specs drive, and the
  // OTHER project's server must still come up over an empty build dir without a startup timeout.
  webServer: [
    {
      command: 'node serve.mjs',
      port: PORT,
      reuseExistingServer: !process.env.CI,
      timeout: 60_000,
    },
    {
      command: 'node serve.mjs',
      port: DEVAPI_PORT,
      env: { NT_SHOWCASE_DIR: DEVAPI_DIR, NT_SHOWCASE_PORT: String(DEVAPI_PORT) },
      reuseExistingServer: !process.env.CI,
      timeout: 60_000,
    },
  ],
  projects: [
    {
      name: 'chromium',
      testIgnore: '**/devapi.spec.ts',
      use: {
        ...devices['Desktop Chrome'],
        launchOptions: chromiumGl,
      },
    },
    {
      name: 'devapi-chromium',
      testMatch: '**/devapi.spec.ts',
      use: {
        ...devices['Desktop Chrome'],
        baseURL: `http://localhost:${DEVAPI_PORT}`,
        launchOptions: chromiumGl,
      },
    },
  ],
});
