// Minimal static server for the ui_showcase wasm build. Adds COOP/COEP (cross-origin isolation,
// harmless for the single-threaded baseline build, required if a threaded build is ever served) and
// the correct application/wasm MIME so the streaming instantiate path works. No dependency beyond Node.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Build dir: env override (CI) or the local wasm-debug example output.
const ROOT =
  process.env.NT_SHOWCASE_DIR ||
  join(__dirname, '..', '..', 'build', 'examples', 'ui_showcase', 'wasm-debug');
const PORT = Number(process.env.NT_SHOWCASE_PORT || 8123);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json; charset=utf-8',
  '.ntpack': 'application/octet-stream',
  '.png': 'image/png',
  '.css': 'text/css; charset=utf-8',
};

const server = createServer(async (req, res) => {
  res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  res.setHeader('Cross-Origin-Resource-Policy', 'cross-origin');

  let urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
  if (urlPath === '/') urlPath = '/index.html';
  // Contain the path inside ROOT (no traversal).
  const filePath = normalize(join(ROOT, urlPath));
  if (!filePath.startsWith(normalize(ROOT))) {
    res.statusCode = 403;
    res.end('forbidden');
    return;
  }
  try {
    const data = await readFile(filePath);
    res.setHeader('Content-Type', MIME[extname(filePath)] || 'application/octet-stream');
    res.end(data);
  } catch {
    res.statusCode = 404;
    res.end('not found');
  }
});

server.listen(PORT, () => {
  console.log(`ui_showcase static server: http://localhost:${PORT}/ (root: ${ROOT})`);
});
