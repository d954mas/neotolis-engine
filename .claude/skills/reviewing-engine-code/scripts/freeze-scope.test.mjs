import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const script = path.join(here, 'freeze-scope.mjs');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'v13-freeze-'));
const repo = path.join(tmp, 'repo');
const out = path.join(tmp, 'out');
fs.mkdirSync(repo);
const git = (...a) => execFileSync('git', a, { cwd: repo, encoding: 'utf8' });
git('init', '-b', 'master');
git('config', 'user.email', 't@t');
git('config', 'user.name', 't');
fs.mkdirSync(path.join(repo, 'engine'), { recursive: true });
fs.writeFileSync(path.join(repo, 'engine', 'a.c'), 'int base(void) { return 1; }\n');
fs.writeFileSync(path.join(repo, 'engine', 'a.h'), '#define A_MAX 4\n');
fs.writeFileSync(path.join(repo, 'engine', 'stable.c'), Array.from({ length: 20 }, (_, i) => `int fn${i}(void) { return ${i}; }`).join('\n') + '\n');
fs.writeFileSync(path.join(repo, 'AGENTS.md'), '# rules at base\n');
git('add', '-A');
git('commit', '-m', 'base', '-q');
git('checkout', '-q', '-b', 'feature');
fs.writeFileSync(path.join(repo, 'engine', 'a.c'), 'int base(void) { return my_parse(0); }\n');
fs.writeFileSync(path.join(repo, 'engine', 'a.h'), '#define A_MAX 8\n');
git('add', '-A');
git('commit', '-m', 'feat', '-q');

const run = (args) => {
  try {
    return { code: 0, stdout: execFileSync('node', [script, ...args], { encoding: 'utf8' }) };
  } catch (e) {
    return { code: e.status, stderr: String(e.stderr) };
  }
};

// happy path
{
  const r = run(['--repo', repo, '--base', 'master', '--out', out]);
  assert.strictEqual(r.code, 0, r.stderr);
  const scope = JSON.parse(fs.readFileSync(path.join(out, 'scope.json'), 'utf8'));
  assert.strictEqual(scope.branch, 'feature');
  assert.match(scope.assignment_hash, /^[0-9a-f]{12}$/);
  const inv = JSON.parse(fs.readFileSync(path.join(out, 'inventory.json'), 'utf8'));
  assert.strictEqual(inv.length, 2);
  const hints = fs.readFileSync(path.join(out, 'routing-hint.tsv'), 'utf8');
  assert.ok(hints.includes('HINT\tarchitecture\tpermanent'));
  assert.ok(hints.includes('FORCE\tuntrusted-input'), 'my_parse( call must force untrusted-input');
  assert.ok(hints.includes('abi-layout'), 'A_MAX macro change in header hints abi-layout');
  assert.ok(fs.readFileSync(path.join(out, 'diff.patch'), 'utf8').includes('my_parse'));
  assert.strictEqual(fs.readFileSync(path.join(out, 'agents-base.md'), 'utf8'), '# rules at base\n', 'AGENTS.md frozen from BASE');
  assert.strictEqual(JSON.parse(fs.readFileSync(path.join(out, 'scope.json'), 'utf8')).agents_base, true);
}

// rename with old_path, binary flag, symbols.json, diff-by-file
{
  git('checkout', '-q', 'feature');
  fs.writeFileSync(path.join(repo, 'engine', 'blob.bin'), Buffer.from([0, 1, 2, 255, 0, 7]));
  git('mv', 'engine/stable.c', 'engine/renamed.c');
  git('add', '-A');
  git('commit', '-m', 'rename+binary', '-q');
  const out5 = path.join(tmp, 'out5');
  const r = run(['--repo', repo, '--base', 'master', '--out', out5]);
  assert.strictEqual(r.code, 0, r.stderr);
  const inv = JSON.parse(fs.readFileSync(path.join(out5, 'inventory.json'), 'utf8'));
  const ren = inv.find((e) => e.path === 'engine/renamed.c');
  assert.ok(ren && ren.status === 'R' && ren.old_path === 'engine/stable.c', 'rename keeps old_path');
  const bin = inv.find((e) => e.path === 'engine/blob.bin');
  assert.ok(bin && bin.binary === true, 'binary file flagged');
  const syms = JSON.parse(fs.readFileSync(path.join(out5, 'symbols.json'), 'utf8'));
  assert.ok(Object.keys(syms).length >= 0, 'symbols.json exists');
  assert.ok(!fs.existsSync(path.join(out5, 'diff-by-file')), 'per-file slices no longer emitted (dead artifact removed)');
  const sc5 = JSON.parse(fs.readFileSync(path.join(out5, 'scope.json'), 'utf8'));
  assert.strictEqual(sc5.function_context, false, 'diff mode recorded');
  assert.ok(r.stdout.includes('tok/reader'), 'size estimate printed');
  git('reset', '-q', '--hard', 'HEAD~1');
}

// --skill-dir freezes references into OUTPUT
{
  const skillDir = path.resolve(here, '..');
  const out6 = path.join(tmp, 'out6');
  const r = run(['--repo', repo, '--base', 'master', '--out', out6, '--skill-dir', skillDir]);
  assert.strictEqual(r.code, 0, r.stderr);
  assert.ok(fs.existsSync(path.join(out6, 'references', 'question-generator.md')), 'references frozen into OUTPUT');
}

// dirty tracked tree aborts
{
  fs.writeFileSync(path.join(repo, 'engine', 'a.c'), 'int dirty(void) { return 0; }\n');
  const r = run(['--repo', repo, '--base', 'master', '--out', path.join(tmp, 'out2')]);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('dirty'), r.stderr);
  git('checkout', '--', '.');
}

// untracked files do NOT abort
{
  fs.writeFileSync(path.join(repo, 'untracked.txt'), 'x\n');
  const r = run(['--repo', repo, '--base', 'master', '--out', path.join(tmp, 'out3')]);
  assert.strictEqual(r.code, 0, r.stderr);
}

// OUTPUT inside the repo is rejected
{
  const r = run(['--repo', repo, '--base', 'master', '--out', path.join(repo, 'out')]);
  assert.notStrictEqual(r.code, 0);
  assert.ok(r.stderr.includes('inside the repository'), r.stderr);
}

// BASE == HEAD rejected
{
  git('checkout', '-q', 'master');
  const r = run(['--repo', repo, '--base', 'master', '--out', path.join(tmp, 'out4')]);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('BASE == HEAD'), r.stderr);
  git('checkout', '-q', 'feature');
}

console.log('freeze-scope.test OK');
