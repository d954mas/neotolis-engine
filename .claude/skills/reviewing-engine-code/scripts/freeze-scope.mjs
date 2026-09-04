#!/usr/bin/env node
// Stage 0: freeze BASE/HEAD scope into OUTPUT. Aborts on a dirty tracked tree so
// lenses (working tree) and verifier (committed HEAD) read identical bytes.

import crypto from 'node:crypto';
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import { resolveOutDir, writeArtifact } from './outdir.mjs';
import { formatRow } from './tsv.mjs';

function arg(name) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? process.argv[i + 1] : undefined;
}

const repo = arg('repo');
if (!repo) {
  console.error('usage: freeze-scope.mjs --repo <root> [--base <sha>] --out <dir> [--skill-dir <dir>] [--function-context]');
  process.exit(2);
}
// stderr ignored: the merge-base ref probe legitimately misses refs and its
// "fatal: Not a valid object name" noise reads as failure to a conductor.
const git = (...a) => execFileSync('git', a, { cwd: repo, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] });

const repoRoot = git('rev-parse', '--show-toplevel').trim();
const out = resolveOutDir(arg('out'), repoRoot);

const dirty = git('status', '--porcelain', '--untracked-files=no').trim();
if (dirty) {
  console.error('freeze-scope: tracked tree is dirty — lens/verifier reads would diverge from HEAD:');
  console.error(dirty);
  process.exit(1);
}

let base = arg('base');
if (!base) {
  for (const ref of ['origin/master', 'master', 'origin/main', 'main']) {
    try {
      base = git('merge-base', ref, 'HEAD').trim();
      break;
    } catch {
      /* try next ref */
    }
  }
}
if (!base) {
  console.error('freeze-scope: cannot resolve BASE (no master/main ref); pass --base');
  process.exit(1);
}
base = git('rev-parse', base).trim();
const head = git('rev-parse', 'HEAD').trim();
const branch = git('branch', '--show-current').trim() || `detached-${head.slice(0, 10)}`;
if (base === head) {
  console.error('freeze-scope: BASE == HEAD, nothing to review');
  process.exit(1);
}

// -W (optional, default OFF): micro-benchmark showed -13% per-lens nc but the full
// pipeline did NOT reproduce it; the mode changes the assignment hash, so it is
// recorded in scope.json for mismatch diagnostics.
const funcCtx = process.argv.includes('--function-context');
const diff = git('diff', ...(funcCtx ? ['-W'] : []), `${base}...${head}`);
const manifest = git('diff', '--name-status', `${base}...${head}`);
const commits = git('log', '--oneline', `${base}..${head}`);
const assignmentHash = crypto.createHash('sha256').update(diff).digest('hex').slice(0, 12);

// #region diff sections — one per file, used for binary flags, symbols, per-file patches
const sections = [];
for (const raw of diff.split(/^(?=diff --git )/m)) {
  if (!raw.startsWith('diff --git ')) continue;
  const bLine = raw.match(/^\+\+\+ b\/(.+)$/m);
  const aLine = raw.match(/^--- a\/(.+)$/m);
  // Binary and pure-rename sections carry no +++/--- lines — fall back to the header.
  const header = raw.match(/^diff --git a\/.+ b\/(.+)$/m);
  const filePath = bLine ? bLine[1] : aLine ? aLine[1] : header ? header[1] : null;
  if (!filePath) continue;
  const binary = /^Binary files |^GIT binary patch/m.test(raw);
  const symbols = [...raw.matchAll(/^@@ [^@]+ @@ (.+)$/gm)].map((m) => m[1].trim());
  sections.push({ path: filePath, raw, binary, symbols: [...new Set(symbols)] });
}
const byPath = new Map(sections.map((s) => [s.path, s]));
// #endregion

const inventory = manifest
  .split('\n')
  .filter(Boolean)
  .map((l) => {
    const [status, ...paths] = l.split('\t');
    const entry = { status: status[0], path: paths[paths.length - 1] };
    // Rename/copy keep the BASE-side name — the verifier needs it for `git show BASE:`.
    if ((status[0] === 'R' || status[0] === 'C') && paths.length > 1) entry.old_path = paths[0];
    const sec = byPath.get(entry.path);
    if (sec && sec.binary) entry.binary = true;
    return entry;
  });

// #region routing hints — hint-only except FORCE rows (mechanical triggers)
const hints = [
  ['HINT', 'architecture', 'permanent'],
  ['HINT', 'behavior-contracts', 'permanent'],
];
const paths = inventory.map((e) => e.path);
const added = diff
  .split('\n')
  .filter((l) => l.startsWith('+') && !l.startsWith('+++'))
  .join('\n');
const pathHas = (re) => paths.some((p) => re.test(p));
const hit = (re) => re.test(added);

// FORCE untrusted-input: changed code enters vendored deps or parses external bytes.
const depsCall = pathHas(/^deps\//) || hit(/#include\s+["<][^">]*deps\//) || hit(/\bcJSON_\w+|\bcgltf_\w+|\bstbi?_\w+/);
const parserish = hit(/\b\w*(parse|decode|deserial)\w*\s*\(/i);
if (depsCall || parserish) {
  hints.push(['FORCE', 'untrusted-input', depsCall ? 'deps-boundary' : 'parser-or-decoder']);
}
if (pathHas(/\.h$/) && hit(/\btypedef\s+struct|\benum\s+\w*\s*\{|#define\s+\w*(MAX|COUNT|SIZE|LIMIT)\w*/)) {
  hints.push(['HINT', 'abi-layout', 'public-header-decl-change']);
}
if (hit(/NT_ASSERT|NT_BUILD_ASSERT|_ERR_|error/i)) hints.push(['HINT', 'validation-failure-policy', 'failure-path-change']);
if (hit(/\bcache|serial|handle|registry|pool|lifetime|invalidate/i)) hints.push(['HINT', 'state-data-lifecycle', 'state-keywords']);
if (pathHas(/CMakeLists|\.cmake$|\/web\//) || hit(/EM_JS|emscripten/i)) hints.push(['HINT', 'platform-toolchain', 'build-or-platform-change']);
if (pathHas(/^tests\//)) hints.push(['HINT', 'test-oracle', 'tests-changed']);
if (hit(/\bthread|mutex|atomic|reentran|callback/i)) hints.push(['HINT', 'concurrency', 'concurrency-keywords']);
if (hit(/\bmalloc|alloc\(|per.frame|hot.path/i) || pathHas(/render|frame/)) hints.push(['HINT', 'performance-size', 'alloc-or-hotpath-keywords']);
// #endregion

// Spec-chapter hits for changed modules, frozen as a fact so ten lenses don't each
// re-derive "no spec chapter exists" (~30 wasted requests measured in run 2).
const moduleNames = [...new Set(paths.map((p) => p.split('/').slice(0, 2).join('/')).concat(paths.map((p) => path.basename(p).replace(/\.[^.]+$/, ''))))]
  .map((s) => s.split('/').pop())
  .filter((s) => s && s.length > 2);
let specHits = [];
try {
  const specFiles = git('ls-files', 'docs/spec').split('\n').filter(Boolean);
  specHits = specFiles.filter((f) => moduleNames.some((m) => f.toLowerCase().includes(m.toLowerCase())));
} catch {
  /* no docs/spec tree */
}

// Rules must come from BASE: the branch under review can edit HEAD's AGENTS.md,
// and the mapper consumes this file AS rules, not as evidence.
let agentsBase = false;
try {
  writeArtifact(out, 'agents-base.md', git('show', `${base}:AGENTS.md`));
  agentsBase = true;
} catch {
  /* repo has no AGENTS.md at BASE — mapper routes from contracts alone */
}

writeArtifact(out, 'scope.json', JSON.stringify({
  repo: repoRoot,
  base,
  head,
  branch,
  assignment_hash: assignmentHash,
  function_context: funcCtx,
  agents_base: agentsBase,
  tree_verified_clean: true,
  spec_chapters_matching_scope: specHits.length ? specHits : 'none',
  generated_by: 'freeze-scope v13.1',
}, null, 2));
writeArtifact(out, 'diff.patch', diff);
writeArtifact(out, 'manifest.txt', manifest);
writeArtifact(out, 'commits.txt', commits);
writeArtifact(out, 'inventory.json', JSON.stringify(inventory, null, 2));
writeArtifact(out, 'routing-hint.tsv', hints.map((h) => formatRow(h, 'routing-hint')).join('\n') + '\n');

// --skill-dir automates the two stage-0 conductor copies (role inputs must live in
// OUTPUT so no agent touches skill dirs): references/ from the CONDUCTING skill,
// principle-catalog from the REVIEWED checkout (its snapshot is the reviewed truth).
const skillDir = arg('skill-dir');
if (skillDir) {
  const refSrc = path.join(skillDir, 'references');
  fs.cpSync(refSrc, path.join(out, 'references'), { recursive: true });
  const cat = path.join(repoRoot, '.claude', 'skills', 'reviewing-engine-code', 'references', 'principle-catalog.md');
  if (fs.existsSync(cat)) fs.copyFileSync(cat, path.join(out, 'principle-catalog.md'));
  else console.error('freeze-scope: NOTE — reviewed repo has no principle-catalog.md; architecture lens runs without it');
}

const symbolMap = Object.fromEntries(sections.filter((s) => s.symbols.length).map((s) => [s.path, s.symbols]));
writeArtifact(out, 'symbols.json', JSON.stringify(symbolMap, null, 2));

const estTokens = Math.round(diff.length / 3.5);
const sizeNote = diff.length > 300_000 ? '  WARNING: large diff — every reader pays this' : '';
console.log(`frozen ${base.slice(0, 10)}...${head.slice(0, 10)} (${branch}) hash=${assignmentHash} files=${inventory.length} diff=${(diff.length / 1024).toFixed(0)}KB (~${estTokens.toLocaleString()} tok/reader)${sizeNote} -> ${out}`);
