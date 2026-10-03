import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const SCRIPT = path.join(path.dirname(fileURLToPath(import.meta.url)), 'wf-extract.mjs');

function run(args, { expectFail = false } = {}) {
  try {
    const stdout = execFileSync(process.execPath, [SCRIPT, ...args], { encoding: 'utf8' });
    assert.equal(expectFail, false, `expected failure, got: ${stdout}`);
    return JSON.parse(stdout);
  } catch (e) {
    if (!expectFail) throw e;
    return String(e.stderr || e.message);
  }
}

function mkOut(files) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wfx-'));
  for (const [name, content] of Object.entries(files)) fs.writeFileSync(path.join(dir, name), content);
  return dir;
}

test('freshcheck: absent -> created+marker; marker -> pass; stale -> fail', () => {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'wfx-fresh-'));
  const out = path.join(base, 'out');
  execFileSync(process.execPath, [SCRIPT, 'freshcheck', '--out', out]); // absent: creates dir+marker
  assert.ok(fs.existsSync(path.join(out, '.h77-run')));
  execFileSync(process.execPath, [SCRIPT, 'freshcheck', '--out', out]); // marker present: retry passes
  fs.writeFileSync(path.join(out, 'scope.json'), '{}'); // artifacts + marker = same run, still passes
  execFileSync(process.execPath, [SCRIPT, 'freshcheck', '--out', out]);
  fs.rmSync(path.join(out, '.h77-run')); // stale review shape: artifacts, no marker
  assert.throws(() => execFileSync(process.execPath, [SCRIPT, 'freshcheck', '--out', out], { stdio: 'pipe' }), /not fresh/);
});

const SCOPE = JSON.stringify({
  repo: '/r', base: 'B1', head: 'H1', branch: 'br',
  assignment_hash: 'hash1', function_context: false, agents_base: true,
});

test('scope: emits the frozen fields', () => {
  const dir = mkOut({ 'scope.json': SCOPE });
  const s = run(['scope', '--out', dir]);
  assert.deepEqual(s, {
    repo: '/r', base: 'B1', head: 'H1', branch: 'br',
    assignment_hash: 'hash1', function_context: false, agents_base: true,
    principle_catalog: false,
  });
  fs.writeFileSync(path.join(dir, 'principle-catalog.md'), '# catalog');
  assert.equal(run(['scope', '--out', dir]).principle_catalog, true);
});

test('mapper: per-lens contract + verbatim QID rows; unknown QID fails', () => {
  const questions = [
    'Q1\ta.c\tlabel-a\tprobe one',
    'Q2\tb.c\tlabel-b\tprobe two',
    'STATUS\tmapper\thash1\tquestions=2',
  ].join('\n');
  const routing = [
    'LENS\tarchitecture\tQ1,Q2\tcheck composition seams',
    'LENS\tbehavior-contracts\tQ2\tcheck contract drift',
    'DROP\tconcurrency\t-\tno threads or callbacks changed anywhere in scope',
    'STATUS\tmapper\thash1\tlenses=2\tdrops=1\trules=present',
  ].join('\n');
  const dir = mkOut({ 'questions.tsv': questions, 'routing.tsv': routing });
  const m = run(['mapper', '--out', dir]);
  assert.equal(m.lenses.length, 2);
  assert.deepEqual(m.lenses[0], {
    id: 'architecture', contract: 'check composition seams', qids: ['Q1', 'Q2'],
    qid_rows: ['Q1\ta.c\tlabel-a\tprobe one', 'Q2\tb.c\tlabel-b\tprobe two'],
  });
  assert.deepEqual(m.drops, [{ id: 'concurrency', reason: 'no threads or callbacks changed anywhere in scope' }]);

  const bad = mkOut({ 'questions.tsv': questions, 'routing.tsv': 'LENS\tarchitecture\tQ9\tc\n' });
  const err = run(['mapper', '--out', bad], { expectFail: true });
  assert.match(err, /unknown QIDs Q9/);
});

test('batches: reads batch-map.json and requires each packet file', () => {
  const map = JSON.stringify({ b1: ['C001', 'C002'], b2: ['C003'] });
  const dir = mkOut({
    'batch-map.json': map,
    'verifier-packet-b1.tsv': 'C001\ta.c:1\tm\tt\tw\te\nC002\ta.c:2\tm\tt\tw\te\n',
    'verifier-packet-b2.tsv': 'C003\tb.c:1\tm\tt\tw\te\n',
  });
  const b = run(['batches', '--out', dir]);
  assert.deepEqual(b.batches.map((x) => [x.name, x.rows]), [['b1', 2], ['b2', 1]]);

  fs.rmSync(path.join(dir, 'verifier-packet-b2.tsv'));
  const err = run(['batches', '--out', dir], { expectFail: true });
  assert.match(err, /verifier-packet-b2\.tsv is missing/);
});

const PACKET3 = 'C001\ta.c:1\tm\tt\tw\te\nC002\ta.c:9\tm\tt\tw\te\nC003\ta.c:10\tm\tt\tw\te\n';
const VROW = (id) => `${id}\tCONFIRMED\tP1\tt->o\ta.c:1\t-\t-`;

test('classify: present / truncated / malformed / missing', () => {
  const dir = mkOut({ 'verifier-packet-b1.tsv': PACKET3 });
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'missing');

  const green = [VROW('C001'), VROW('C002'), VROW('C003'), 'STATUS\tverifier-b1\thash1\tverdicts=3'].join('\n');
  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), green);
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'present');

  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), [VROW('C001'), VROW('C002')].join('\n'));
  const t = run(['classify', '--out', dir, '--batch', 'b1']);
  assert.equal(t.state, 'truncated');
  assert.deepEqual(t.missing, ['C003']);

  // STATUS present but not last is truncation-shaped corruption -> truncated
  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), ['STATUS\tverifier-b1\thash1\tverdicts=3', VROW('C001'), VROW('C002'), VROW('C003')].join('\n'));
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'truncated');

  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), [VROW('C001'), VROW('C999'), 'STATUS\tverifier-b1\thash1\tverdicts=2'].join('\n'));
  // Below-packet ID set wins over the unknown ID: a cut can mangle the last row's ID,
  // and truncation must never route to a same-size retry.
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'truncated');

  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), [VROW('C001'), VROW('C002'), VROW('C999'), 'STATUS\tverifier-b1\thash1\tverdicts=3'].join('\n'));
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'truncated'); // C003 still missing

  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), [VROW('C001'), VROW('C002'), VROW('C003'), VROW('C999'), 'STATUS\tverifier-b1\thash1\tverdicts=4'].join('\n'));
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'malformed'); // full coverage + alien ID
});

test('classify: declared-error stub and CRLF tolerance', () => {
  const dir = mkOut({ 'verifier-packet-b1.tsv': PACKET3 });
  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), 'STATUS\tverifier-b1\thash1\tverdicts=0\terror=hash mismatch\n');
  const e = run(['classify', '--out', dir, '--batch', 'b1']);
  assert.equal(e.state, 'error');
  assert.equal(e.reason, 'hash mismatch');

  // CRLF + trailing blank line must not create phantom rows (v13 tsv.mjs accepts this file)
  const green = [VROW('C001'), VROW('C002'), VROW('C003'), 'STATUS\tverifier-b1\thash1\tverdicts=3'].join('\r\n') + '\r\n';
  fs.writeFileSync(path.join(dir, 'verdicts-b1.tsv'), green);
  assert.equal(run(['classify', '--out', dir, '--batch', 'b1']).state, 'present');
});

test('split: multi-file batch cuts at the file boundary nearest to half', () => {
  const packet = [
    'C001\ta.c:1\tm\tt\tw\te',
    'C002\ta.c:2\tm\tt\tw\te',
    'C003\tb.c:1\tm\tt\tw\te',
    'C004\tc.c:1\tm\tt\tw\te',
  ].join('\n');
  const dir = mkOut({
    'batch-map.json': JSON.stringify({ b1: ['C001', 'C002', 'C003', 'C004'], b2: ['C009'] }),
    'verifier-packet-b1.tsv': packet,
  });
  const s = run(['split', '--out', dir, '--batch', 'b1']);
  assert.deepEqual(s.sub_batches, ['b1a', 'b1b']);
  assert.deepEqual(s.keep, ['b2']);
  const newMap = JSON.parse(fs.readFileSync(s.map_file, 'utf8'));
  assert.deepEqual(newMap, { b1a: ['C001', 'C002'], b1b: ['C003', 'C004'], b2: ['C009'] });
});

test('split: single-file run cuts at the largest anchor-line gap', () => {
  const packet = [
    'C001\ta.c:10\tm\tt\tw\te',
    'C002\ta.c:12\tm\tt\tw\te',
    'C003\ta.c:200\tm\tt\tw\te',
    'C004\ta.c:210\tm\tt\tw\te',
  ].join('\n');
  const dir = mkOut({
    'batch-map.json': JSON.stringify({ b1: ['C001', 'C002', 'C003', 'C004'] }),
    'verifier-packet-b1.tsv': packet,
  });
  const s = run(['split', '--out', dir, '--batch', 'b1']);
  const newMap = JSON.parse(fs.readFileSync(s.map_file, 'utf8'));
  assert.deepEqual(newMap.b1a, ['C001', 'C002']);
  assert.deepEqual(newMap.b1b, ['C003', 'C004']);
});

test('split: lopsided file boundary falls back to the in-run line gap', () => {
  // FFD shape: 5-row a.c run + 1-row z.c filler; the only boundary is 5/1 —
  // splitting there guarantees the big side re-truncates.
  const ids = ['C001', 'C002', 'C003', 'C004', 'C005', 'C006'];
  const packet = [
    'C001\ta.c:10\tm\tt\tw\te',
    'C002\ta.c:11\tm\tt\tw\te',
    'C003\ta.c:12\tm\tt\tw\te',
    'C004\ta.c:500\tm\tt\tw\te',
    'C005\ta.c:501\tm\tt\tw\te',
    'C006\tz.c:1\tm\tt\tw\te',
  ].join('\n');
  const dir = mkOut({
    'batch-map.json': JSON.stringify({ b1: ids }),
    'verifier-packet-b1.tsv': packet,
  });
  const s = run(['split', '--out', dir, '--batch', 'b1']);
  const newMap = JSON.parse(fs.readFileSync(s.map_file, 'utf8'));
  assert.deepEqual(newMap.b1a, ['C001', 'C002', 'C003']);
  assert.deepEqual(newMap.b1b, ['C004', 'C005', 'C006']);
});

test('split: refuses name collisions and one-row batches', () => {
  const dir = mkOut({
    'batch-map.json': JSON.stringify({ b1: ['C001', 'C002'], b1a: ['C009'] }),
    'verifier-packet-b1.tsv': 'C001\ta.c:1\tm\tt\tw\te\nC002\ta.c:2\tm\tt\tw\te\n',
  });
  assert.match(run(['split', '--out', dir, '--batch', 'b1'], { expectFail: true }), /name collision/);

  const dir2 = mkOut({
    'batch-map.json': JSON.stringify({ b1: ['C001'] }),
    'verifier-packet-b1.tsv': 'C001\ta.c:1\tm\tt\tw\te\n',
  });
  assert.match(run(['split', '--out', dir2, '--batch', 'b1'], { expectFail: true }), /nothing to split/);
});
