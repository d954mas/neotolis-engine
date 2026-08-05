import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const script = path.join(here, 'pack-candidates.mjs');
const HASH = 'abc123def456';

function fixture(extraScope = {}) {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'v13-pack-'));
  fs.writeFileSync(path.join(out, 'scope.json'), JSON.stringify({ assignment_hash: HASH, ...extraScope }));
  fs.writeFileSync(path.join(out, 'inventory.json'), JSON.stringify([
    { status: 'M', path: 'engine/a.c' },
    { status: 'A', path: 'engine/b.c' },
  ]));
  return out;
}
const lensFile = (rows, id, read, unread = 'none') => {
  const nCand = rows.filter((r) => !/^Q\d+$/.test(r[0])).length;
  return rows.map((r) => r.join('\t')).join('\n') + (rows.length ? '\n' : '') +
    `STATUS\t${id}\t${HASH}\trows=${nCand}\tread=${read}\tunread=${unread}\n`;
};
const run = (out, expectArg, extra = []) => {
  const args = [script, '--out', out, ...(expectArg ? ['--expect', expectArg] : []), ...extra];
  try {
    return { code: 0, stdout: execFileSync('node', args, { encoding: 'utf8' }) };
  } catch (e) {
    return { code: e.status, stderr: String(e.stderr) };
  }
};

// happy path + dedup keeps the LONGEST evidence, provenance goes to private/
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'mech1', 'trig1', 'Wrong  Thing happens', 'short-ev', '-'],
    ['A2', 'engine/a.c:10', 'mech1b', 'trig1b', 'a DIFFERENT wrong thing', 'ev2', 'fact2'],
  ], 'a', 'engine/a.c,engine/b.c'));
  fs.writeFileSync(path.join(out, 'lens-b.tsv'), lensFile([
    ['B1', 'engine/a.c:10', 'mech-dup', 'trig-dup', 'wrong thing HAPPENS', 'much-longer-evidence-with-callsite-proof', '-'],
  ], 'b', 'engine/a.c', 'engine/b.c'));
  const r = run(out, 'a,b');
  assert.strictEqual(r.code, 0, r.stderr);
  const cands = fs.readFileSync(path.join(out, 'candidates.tsv'), 'utf8').trim().split('\n');
  assert.strictEqual(cands.length, 2, 'dedup by (anchor, outcome), distinct outcomes preserved');
  const merged = cands.find((l) => l.includes('much-longer-evidence'));
  assert.ok(merged && merged.includes('mech-dup'), 'merge keeps the whole strongest row, not the routing-order winner');
  const prov = JSON.parse(fs.readFileSync(path.join(out, 'private', 'provenance.json'), 'utf8'));
  assert.strictEqual(prov.candidates.C001.sources.length, 2, 'merged row keeps both provenances');
  assert.ok(!fs.readFileSync(path.join(out, 'verifier-packet.tsv'), 'utf8').includes('fact2'), 'missing_fact stays private');
}

// coverage hard fail
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
  ], 'a', 'engine/a.c', 'engine/b.c'));
  const r = run(out, 'a');
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('COVERAGE FAIL') && r.stderr.includes('engine/b.c'), r.stderr);
}

// hash mismatch / missing artifact / range anchor / cross-paste id / bad partition / dup ids
{
  const out = fixture();
  const bad = lensFile([['A1', 'engine/a.c:1', 'm', 't', 'w', 'e', '-']], 'a', 'engine/a.c,engine/b.c').replace(HASH, 'wronghash999');
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), bad);
  assert.ok(run(out, 'a').stderr.includes('wrong scope'));
  fs.rmSync(path.join(out, 'lens-a.tsv'));
  assert.ok(run(out, 'a').stderr.includes('missing artifact'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([['A1', 'engine/a.c:10-14', 'm', 't', 'w', 'e', '-']], 'a', 'engine/a.c,engine/b.c'));
  assert.ok(run(out, 'a').stderr.includes('not file:line'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-']], 'zzz', 'engine/a.c,engine/b.c'));
  assert.ok(run(out, 'a').stderr.includes('cross-pasted'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-']], 'a', 'engine/a.c'));
  assert.ok(run(out, 'a').stderr.includes('missing from read/unread partition'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['A1', 'engine/a.c:11', 'm2', 't2', 'w2', 'e2', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  assert.ok(run(out, 'a').stderr.includes('duplicate candidate ids'));
}

// old_path of a rename is allowed in read= (not required)
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'inventory.json'), JSON.stringify([
    { status: 'M', path: 'engine/a.c' },
    { status: 'R', path: 'engine/b.c', old_path: 'engine/old_b.c' },
  ]));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
  ], 'a', 'engine/a.c,engine/b.c,engine/old_b.c'));
  const r = run(out, 'a');
  assert.strictEqual(r.code, 0, r.stderr);
}

// QID dispositions vs routing.tsv; foreign-QID disposition rejected; --expect must match routed set
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'routing.tsv'),
    'LENS\ta\tQ01,Q02\tcontract stmt\nSTATUS\tmapper\t' + HASH + '\tlenses=1\tdrops=0\trules=present\n');
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['Q01', 'A1', '-', '-', '-', '-', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  assert.ok(run(out, 'a').stderr.includes('Q02 has no disposition row'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['Q01', 'A1', '-', '-', '-', '-', '-'],
    ['Q02', 'clear', '-', '-', '-', '-', '-'],
    ['Q09', 'clear', '-', '-', '-', '-', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  assert.ok(run(out, 'a').stderr.includes('not assigned to this lens'));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['Q01', 'A1', '-', '-', '-', '-', '-'],
    ['Q02', 'clear', '-', '-', '-', '-', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const ok = run(out, undefined);
  assert.strictEqual(ok.code, 0, ok.stderr);
  const prov = JSON.parse(fs.readFileSync(path.join(out, 'private', 'provenance.json'), 'utf8'));
  assert.strictEqual(prov.qid_dispositions.a.Q02, 'clear');
  // --expect narrowing against the routed set must fail without --allow-partial
  fs.writeFileSync(path.join(out, 'routing.tsv'),
    'LENS\ta\tQ01,Q02\tstmt\nLENS\tb\tQ01\tstmt2\nSTATUS\tmapper\t' + HASH + '\tlenses=2\tdrops=0\trules=present\n');
  const narrow = run(out, 'a');
  assert.strictEqual(narrow.code, 1);
  assert.ok(narrow.stderr.includes('!= routed set'), narrow.stderr);
}

// --not-covered: excluded from expectation, echoed; bogus id rejected; private/ fallback works
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'routing.tsv'),
    'LENS\ta\tQ01\tstmt\nLENS\tb\tQ02\tstmt2\nSTATUS\tmapper\t' + HASH + '\tlenses=2\tdrops=0\trules=present\n');
  fs.mkdirSync(path.join(out, 'private'), { recursive: true });
  // lens-a lives in private/ (post-mv rerun), lens-b is NOT-COVERED
  fs.writeFileSync(path.join(out, 'private', 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['Q01', 'A1', '-', '-', '-', '-', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const bogus = run(out, undefined, ['--not-covered', 'zzz']);
  assert.strictEqual(bogus.code, 1);
  assert.ok(bogus.stderr.includes("not a routed lens"), bogus.stderr);
  const ok = run(out, undefined, ['--not-covered', 'b']);
  assert.strictEqual(ok.code, 0, ok.stderr);
  assert.ok(ok.stdout.includes('NOT-COVERED: b'), ok.stdout);
}

// stale packet files from a previous invocation are removed
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  fs.writeFileSync(path.join(out, 'verifier-packet-stale.tsv'), 'garbage\n');
  const r = run(out, 'a');
  assert.strictEqual(r.code, 0, r.stderr);
  assert.ok(!fs.existsSync(path.join(out, 'verifier-packet-stale.tsv')), 'stale packet removed');
  assert.ok(fs.existsSync(path.join(out, 'verifier-packet.tsv')));
}

// batch map: traversal name, empty batch, double-assignment all rejected
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['A2', 'engine/b.c:5', 'm2', 't2', 'w2', 'e2', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const bm = path.join(out, 'bm.json');
  fs.writeFileSync(bm, JSON.stringify({ 'x/../../ESC': ['C001', 'C002'] }));
  assert.ok(run(out, 'a', ['--batch-map', bm]).stderr.includes('illegal batch name'));
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001', 'C002'], b2: [] }));
  assert.ok(run(out, 'a', ['--batch-map', bm]).stderr.includes('is empty'));
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001', 'C002'], b2: ['C001'] }));
  assert.ok(run(out, 'a', ['--batch-map', bm]).stderr.includes('more than one batch'));
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001'], b2: ['C002'] }));
  const ok = run(out, 'a', ['--batch-map', bm]);
  assert.strictEqual(ok.code, 0, ok.stderr);
  assert.ok(fs.existsSync(path.join(out, 'batch-map.json')), 'canonical batch map persisted');
}

// --auto-batch: file-boundary batching + batch-map.json; no unsuffixed packet in batch mode
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w1', 'e', '-'],
    ['A2', 'engine/a.c:20', 'm', 't', 'w2', 'e', '-'],
    ['A3', 'engine/a.c:30', 'm', 't', 'w3', 'e', '-'],
    ['A4', 'engine/b.c:5', 'm', 't', 'w4', 'e', '-'],
    ['A5', 'engine/b.c:6', 'm', 't', 'w5', 'e', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const r = run(out, 'a', ['--auto-batch', '3']);
  assert.strictEqual(r.code, 0, r.stderr);
  const map = JSON.parse(fs.readFileSync(path.join(out, 'batch-map.json'), 'utf8'));
  assert.deepStrictEqual(Object.keys(map), ['b1', 'b2']);
  assert.strictEqual(map.b1.length, 3, 'a.c run fills b1');
  assert.strictEqual(map.b2.length, 2, 'b.c run stays whole in b2');
  assert.ok(fs.existsSync(path.join(out, 'verifier-packet-b1.tsv')));
  assert.ok(fs.existsSync(path.join(out, 'verifier-packet-b2.tsv')));
  assert.ok(!fs.existsSync(path.join(out, 'verifier-packet.tsv')), 'no unsuffixed packet in batch mode');
}

// --auto-batch: single-file run beyond allowance splits at the largest line gap
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'inventory.json'), JSON.stringify([{ status: 'M', path: 'engine/a.c' }]));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:1', 'm', 't', 'w1', 'e', '-'],
    ['A2', 'engine/a.c:2', 'm', 't', 'w2', 'e', '-'],
    ['A3', 'engine/a.c:3', 'm', 't', 'w3', 'e', '-'],
    ['A4', 'engine/a.c:100', 'm', 't', 'w4', 'e', '-'],
    ['A5', 'engine/a.c:101', 'm', 't', 'w5', 'e', '-'],
  ], 'a', 'engine/a.c'));
  const r = run(out, 'a', ['--auto-batch', '3']);
  assert.strictEqual(r.code, 0, r.stderr);
  const map = JSON.parse(fs.readFileSync(path.join(out, 'batch-map.json'), 'utf8'));
  assert.deepStrictEqual(map.b1, ['C001', 'C002', 'C003'], 'split lands at the 3->100 gap');
  assert.strictEqual(map.b2.length, 2);
}

// --auto-batch: first-fit-decreasing folds small file-runs together instead of tiny tail batches
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'inventory.json'), JSON.stringify([
    { status: 'M', path: 'engine/a.c' },
    { status: 'M', path: 'engine/b.c' },
    { status: 'M', path: 'engine/c.c' },
  ]));
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w1', 'e', '-'],
    ['A2', 'engine/b.c:1', 'm', 't', 'w2', 'e', '-'],
    ['A3', 'engine/b.c:2', 'm', 't', 'w3', 'e', '-'],
    ['A4', 'engine/b.c:3', 'm', 't', 'w4', 'e', '-'],
    ['A5', 'engine/c.c:7', 'm', 't', 'w5', 'e', '-'],
  ], 'a', 'engine/a.c,engine/b.c,engine/c.c'));
  const r = run(out, 'a', ['--auto-batch', '3']);
  assert.strictEqual(r.code, 0, r.stderr);
  const map = JSON.parse(fs.readFileSync(path.join(out, 'batch-map.json'), 'utf8'));
  assert.strictEqual(Object.keys(map).length, 2, 'FFD packs 2 batches, not 3 with a tail');
  assert.deepStrictEqual(map.b1, ['C001', 'C005'], 'small runs folded together');
  assert.deepStrictEqual(map.b2, ['C002', 'C003', 'C004']);
}

// --auto-batch validation: exclusive with --batch-map, integer only; uppercase batch name rejected
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['A2', 'engine/b.c:5', 'm2', 't2', 'w2', 'e2', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const bm = path.join(out, 'bm.json');
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001', 'C002'] }));
  assert.ok(run(out, 'a', ['--auto-batch', '3', '--batch-map', bm]).stderr.includes('mutually exclusive'));
  assert.ok(run(out, 'a', ['--auto-batch', 'x']).stderr.includes('not a positive integer'));
  fs.writeFileSync(bm, JSON.stringify({ MEM: ['C001', 'C002'] }));
  assert.ok(run(out, 'a', ['--batch-map', bm]).stderr.includes('lowercase'), 'NTFS case collision guard');
}

// stale verdicts removed unless --keep-verdicts; kept batch must match its new ID set
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['A2', 'engine/b.c:5', 'm2', 't2', 'w2', 'e2', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  fs.writeFileSync(path.join(out, 'verdicts-old.tsv'), 'x\n');
  const r1 = run(out, 'a');
  assert.strictEqual(r1.code, 0, r1.stderr);
  assert.ok(!fs.existsSync(path.join(out, 'verdicts-old.tsv')), 'stale verdicts removed');
  const bm = path.join(out, 'bm.json');
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001'], b2: ['C002'] }));
  assert.strictEqual(run(out, 'a', ['--batch-map', bm]).code, 0);
  fs.writeFileSync(path.join(out, 'verdicts-b1.tsv'),
    `C001\tCONFIRMED\tP1\tt->o\ta.c:10\t-\t-\nSTATUS\tverifier-b1\t${HASH}\tverdicts=1\n`);
  const r2 = run(out, 'a', ['--batch-map', bm, '--keep-verdicts', 'b1']);
  assert.strictEqual(r2.code, 0, r2.stderr);
  assert.ok(fs.existsSync(path.join(out, 'verdicts-b1.tsv')), 'kept verdict file survives the rerun');
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C002'], b2: ['C001'] }));
  const r3 = run(out, 'a', ['--batch-map', bm, '--keep-verdicts', 'b1']);
  assert.strictEqual(r3.code, 1);
  assert.ok(r3.stderr.includes("ID set != batch 'b1'"), r3.stderr);
  const r4 = run(out, 'a', ['--keep-verdicts', 'b9']);
  assert.strictEqual(r4.code, 1);
  assert.ok(r4.stderr.includes('requires a batch map'), r4.stderr);
  const r5 = run(out, 'a', ['--batch-map', bm, '--keep-verdicts', 'b9']);
  assert.strictEqual(r5.code, 1);
  assert.ok(r5.stderr.includes('no batch of that name'), r5.stderr);
}

// a rejected map destroys NOTHING: validation runs before any deletion
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
    ['A1', 'engine/a.c:10', 'm', 't', 'w', 'e', '-'],
    ['A2', 'engine/b.c:5', 'm2', 't2', 'w2', 'e2', '-'],
  ], 'a', 'engine/a.c,engine/b.c'));
  const bm = path.join(out, 'bm.json');
  fs.writeFileSync(bm, JSON.stringify({ b1: ['C001'], b2: ['C002'] }));
  assert.strictEqual(run(out, 'a', ['--batch-map', bm]).code, 0);
  fs.writeFileSync(path.join(out, 'verdicts-b1.tsv'),
    `C001\tCONFIRMED\tP1\tt->o\ta.c:10\t-\t-\nSTATUS\tverifier-b1\t${HASH}\tverdicts=1\n`);
  fs.writeFileSync(bm, JSON.stringify({ BAD: ['C001', 'C002'] }));
  const r = run(out, 'a', ['--batch-map', bm]);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('illegal batch name'), r.stderr);
  assert.ok(fs.existsSync(path.join(out, 'verdicts-b1.tsv')), 'verdict file survives a rejected map');
  assert.ok(fs.existsSync(path.join(out, 'verifier-packet-b1.tsv')), 'packets survive a rejected map');
}

// repo-backed checks: ghost path, EOF exact, blank line, HEAD mismatch, dirty tree
{
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'v13-packrepo-'));
  const repo = path.join(tmp, 'repo');
  fs.mkdirSync(path.join(repo, 'engine'), { recursive: true });
  const g = (...a) => execFileSync('git', a, { cwd: repo, encoding: 'utf8' });
  g('init', '-b', 'master');
  g('config', 'user.email', 't@t');
  g('config', 'user.name', 't');
  fs.writeFileSync(path.join(repo, 'engine', 'a.c'), 'int f(void) {\n\n  return 1;\n}\n'); // 4 lines, line 2 blank
  fs.writeFileSync(path.join(repo, 'engine', 'b.c'), 'int g(void) { return 2; }\n');
  g('add', '-A');
  g('commit', '-m', 'x', '-q');
  const head = g('rev-parse', 'HEAD').trim();
  const mk = (anchor) => {
    const out = fixture({ repo, head });
    fs.writeFileSync(path.join(out, 'lens-a.tsv'), lensFile([
      ['A1', anchor, 'm', 't', 'w', 'e', '-'],
    ], 'a', 'engine/a.c,engine/b.c'));
    return out;
  };
  assert.ok(run(mk('engine/ghost.c:3'), 'a').stderr.includes('does not exist at HEAD'));
  assert.ok(run(mk('engine/a.c:5'), 'a').stderr.includes('past EOF (4 lines'), 'trailing-newline off-by-one fixed');
  assert.ok(run(mk('engine/a.c:2'), 'a').stderr.includes('blank line'));
  assert.strictEqual(run(mk('engine/a.c:3'), 'a').code, 0);
  const wrongHead = mk('engine/a.c:3');
  const sj = JSON.parse(fs.readFileSync(path.join(wrongHead, 'scope.json'), 'utf8'));
  sj.head = '0'.repeat(40);
  fs.writeFileSync(path.join(wrongHead, 'scope.json'), JSON.stringify(sj));
  assert.ok(run(wrongHead, 'a').stderr.includes('wrong checkout'));
  const dirtyOut = mk('engine/a.c:3');
  fs.writeFileSync(path.join(repo, 'engine', 'b.c'), 'int g(void) { return 3; }\n');
  assert.ok(run(dirtyOut, 'a').stderr.includes('DIRTY during discovery'));
  g('checkout', '--', '.');
}

console.log('pack-candidates.test OK');
