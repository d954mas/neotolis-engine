import { defineConfig, devices } from '@playwright/test';

// The static server serves the ui_showcase wasm-debug build (NT_SHOWCASE_DIR overrides the dir in CI).
const PORT = Number(process.env.NT_SHOWCASE_PORT || 8123);

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
    url: `http://localhost:${PORT}/index.html`,
    reuseExistingServer: !process.env.CI,
    timeout: 60_000,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        // Force the SwiftShader GL backend so WebGL2 works in headless CI (and on GPUs whose
        // headless context is unreliable). The engine needs a real GL2 context for texture upload;
        // without this the context is lost and the wasm app traps on first glTexImage2D.
        launchOptions: {
          args: [
            '--use-gl=angle',
            '--use-angle=swiftshader',
            '--enable-unsafe-swiftshader',
            '--ignore-gpu-blocklist',
          ],
        },
      },
    },
  ],
});
