import { defineConfig, devices } from '@playwright/test';

// One static server for the browser_smoke app (built by the wasm-debug preset). The devapi_host web
// checks are Python scenarios (tools/devapi, --target web), not specs here.
const PORT = Number(process.env.NT_SHOWCASE_PORT || 8123);

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
  webServer: {
    command: 'node serve.mjs',
    port: PORT,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        launchOptions: chromiumGl,
      },
    },
  ],
});
