// Minimal static server for the browser_smoke wasm build. Adds COOP/COEP (cross-origin isolation,
// harmless for the single-threaded baseline build, required if a threaded build is ever served),
// keeps every response same-origin (CORP same-origin to match the require-corp embedder policy --
// the test is a representative single-origin load, not cross-origin), and the correct
// application/wasm MIME so the streaming instantiate path works. No dependency beyond Node.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Build dir: env override (NT_SHOWCASE_DIR) or the local browser_smoke wasm-debug output.
const ROOT =
  process.env.NT_SHOWCASE_DIR ||
  join(__dirname, '..', '..', 'build', 'tests', 'browser', 'app', 'wasm-debug');
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
  res.setHeader('Cross-Origin-Resource-Policy', 'same-origin');

  // Malformed percent-encoding (e.g. /%ZZ) throws URIError; answer 400 instead of crashing the server.
  let urlPath;
  try {
    urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
  } catch {
    res.statusCode = 400;
    res.end('bad request');
    return;
  }
  // nt_http acceptance endpoints (http.spec.ts): byte-exact POST echo + a non-2xx probe.
  if (req.method === 'POST' && urlPath === '/echo') {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      res.setHeader('Content-Type', 'application/octet-stream');
      res.setHeader('X-Echo-Content-Type', req.headers['content-type'] || '');
      res.setHeader('X-Echo-X-Nt-Test', req.headers['x-nt-test'] || '');
      res.end(Buffer.concat(chunks));
    });
    return;
  }
  if (urlPath === '/status404') {
    res.statusCode = 404;
    res.end('missing');
    return;
  }
  if (urlPath === '/slow') {
    // Stalls 5 s: the timeout/cancel browser tests abort long before this fires.
    setTimeout(() => res.end('ok'), 5000);
    return;
  }

  if (urlPath === '/') urlPath = '/index.html';
  // Contain the path inside ROOT (no traversal). Compare against ROOT + sep so a SIBLING dir whose
  // name merely EXTENDS ROOT as a prefix (e.g. "<root>-evil") can't pass; allow exact ROOT too.
  const root = normalize(ROOT);
  const filePath = normalize(join(ROOT, urlPath));
  if (filePath !== root && !filePath.startsWith(root + sep)) {
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
