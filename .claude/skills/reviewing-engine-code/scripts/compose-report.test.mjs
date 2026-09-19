import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const script = path.join(here, 'compose-report.mjs');
const HASH = 'abc123def456';

function fixture() {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'v13-report-'));
  fs.writeFileSync(path.join(out, 'scope.json'), JSON.stringify({
    assignment_hash: HASH, base: 'b'.repeat(40), head: 'h'.repeat(40), branch: 'test-branch', repo: '/r',
  }));
  const rows = [
    ['C001', 'a.c:10', 'null deref', 'trig', 'crash', 'ev'],
    ['C002', 'b.c:20', 'TEST-GAP: no oracle', 'trig', 'false green', 'ev'],
    ['C003', 'c.c:30', 'race', 'trig', 'corrupt', 'ev'],
  ];
  fs.writeFileSync(path.join(out, 'candidates.tsv'), rows.map((r) => r.join('\t')).join('\n') + '\n');
  // Packets and provenance are hard stage-5 requirements (fail-loud on absence).
  fs.writeFileSync(path.join(out, 'verifier-packet-1.tsv'), rows.map((r) => r.join('\t')).join('\n') + '\n');
  fs.mkdirSync(path.join(out, 'private'), { recursive: true });
  fs.writeFileSync(path.join(out, 'private', 'provenance.json'), JSON.stringify({ qid_dispositions: {} }));
  return out;
}
const verdictsFile = (rows) =>
  rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tverifier-1\t${HASH}\tverdicts=${rows.length}\n`;

const run = (out) => {
  try {
    return { code: 0, stdout: execFileSync('node', [script, '--out', out], { encoding: 'utf8' }) };
  } catch (e) {
    return { code: e.status, stderr: String(e.stderr) };
  }
};

// happy path: partition + suggested verdict
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 'trig->crash', 'a.c:10', '-', 'add unit test'],
    ['C002', 'CONFIRMED', 'P2', 'TEST-GAP: trig->green', 'b.c:20', '-', 'mutation check'],
    ['C003', 'REFUTED', 'DROP', 'trig->none', 'c.c:30', 'guard at c.c:25 rejects', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 0, r.stderr);
  const report = fs.readFileSync(path.join(out, 'report.md'), 'utf8');
  assert.ok(report.includes('**merge with fixes**'), 'confirmed P1 -> merge with fixes');
  assert.ok(report.includes('Findings (1)'), 'one implementation finding');
  assert.ok(report.includes('test-oracle gaps (1)'), 'verifier TEST-GAP prefix routes to its own section');
  assert.ok(report.includes('Challenged candidates (1)') && report.includes('KILLED BY: guard at c.c:25'), 'refuted kept with killing fact');
}

// missing verdict fails loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('C002') && r.stderr.includes('C003'), r.stderr);
}

// verdict/severity tokens outside the closed vocabulary fail loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFRIMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'p2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes("'CONFRIMED' outside") && r.stderr.includes("'p2' outside"), r.stderr);
  assert.ok(r.stderr.includes('Respawn the offending verifier batch'), r.stderr);
}

// P0 confirmed -> do not merge; all clean -> merge-ready
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P0', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'PLAUSIBLE', 'P2', 't->o', 'b.c:20', 'fact?', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 0, r.stderr);
  assert.ok(fs.readFileSync(path.join(out, 'report.md'), 'utf8').includes('**do not merge**'));
}

// REFUTED / severity-DROP without a fact in column 6 fail loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'REFUTED', 'DROP', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'DROP', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'CONFIRMED', 'P2', 't->o', 'c.c:30', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('C001 REFUTED without a killing fact'), r.stderr);
  assert.ok(r.stderr.includes('C002 severity DROP without a drop reason'), r.stderr);
  assert.ok(!r.stderr.includes('C003'), 'CONFIRMED P2 with - is legal');
}

// per-batch packet<->verdict ID equality: cross-batch verdicts rejected even when the union is complete
{
  const out = fixture();
  const packetRow = (id, anchor) => [id, anchor, 'm', 't', 'w', 'e'].join('\t');
  const vf = (batch, rows) => rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tverifier-${batch}\t${HASH}\tverdicts=${rows.length}\n`;
  fs.writeFileSync(path.join(out, 'verifier-packet-b1.tsv'), packetRow('C001', 'a.c:10') + '\n' + packetRow('C002', 'b.c:20') + '\n');
  fs.writeFileSync(path.join(out, 'verifier-packet-b2.tsv'), packetRow('C003', 'c.c:30') + '\n');
  fs.writeFileSync(path.join(out, 'verdicts-b1.tsv'), vf('b1', [
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C003', 'CONFIRMED', 'P2', 't->o', 'c.c:30', '-', '-'],
  ]));
  fs.writeFileSync(path.join(out, 'verdicts-b2.tsv'), vf('b2', [
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('verdicts for ids outside its packet: C003'), r.stderr);
  assert.ok(r.stderr.includes('missing verdicts for its own packet ids: C002'), r.stderr);
}

// stale verdict file with no packet + packet with no verdict file both fail loud
{
  const out = fixture();
  const packetRow = (id, anchor) => [id, anchor, 'm', 't', 'w', 'e'].join('\t');
  const vf = (batch, rows) => rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tverifier-${batch}\t${HASH}\tverdicts=${rows.length}\n`;
  fs.writeFileSync(path.join(out, 'verifier-packet-b1.tsv'),
    [packetRow('C001', 'a.c:10'), packetRow('C002', 'b.c:20'), packetRow('C003', 'c.c:30')].join('\n') + '\n');
  fs.writeFileSync(path.join(out, 'verdicts-old.tsv'), vf('old', [
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('no verifier-packet-old.tsv on disk'), r.stderr);
  assert.ok(r.stderr.includes('verifier-packet-b1.tsv has no verdicts-b1.tsv'), r.stderr);
}

// unknown candidate id flagged; one malformed batch does not hide another's vocab error
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
    ['C999', 'CONFIRMED', 'P2', 't->o', 'z.c:1', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('unknown candidate id C999'), r.stderr);

  const out2 = fixture();
  fs.writeFileSync(path.join(out2, 'verdicts-1.tsv'), 'garbage without tabs\n');
  fs.writeFileSync(path.join(out2, 'verdicts-2.tsv'), verdictsFile([
    ['C001', 'CONFRIMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]).replace('verifier-1', 'verifier-2'));
  const r2 = run(out2);
  assert.strictEqual(r2.code, 1);
  assert.ok(r2.stderr.includes('verdicts-1.tsv'), 'malformed file reported');
  assert.ok(r2.stderr.includes("'CONFRIMED' outside"), 'second file still checked');
}

// DUPLICATE-OF: dup nests under primary (visible, not a standalone finding); bad marks fail loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 'trig->crash', 'a.c:10', '-', 'oracle'],
    ['C002', 'CONFIRMED', 'P1', 'DUPLICATE-OF C001: same mech, same consequence', 'a.c:12', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 0, r.stderr);
  const report = fs.readFileSync(path.join(out, 'report.md'), 'utf8');
  assert.ok(report.includes('Findings (1)'), 'dup not counted as its own finding');
  assert.ok(report.includes('C002 `b.c:20`'), 'dup still visible under the primary (anchor from candidates.tsv)');

  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 'trig->crash', 'a.c:10', '-', '-'],
    ['C002', 'PLAUSIBLE', 'P2', 'DUPLICATE-OF C001: same mech', 'a.c:12', 'fact?', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r2 = run(out);
  assert.strictEqual(r2.code, 1);
  assert.ok(r2.stderr.includes('verdict/severity differ'), r2.stderr);

  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 'trig->crash', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P1', 'DUPLICATE-OF C099: same mech', 'a.c:12', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r3 = run(out);
  assert.strictEqual(r3.code, 1);
  assert.ok(r3.stderr.includes('DUPLICATE-OF C099 but no such verdict'), r3.stderr);
}

// DUPLICATE-OF across batches is rejected (a verifier only sees its own packet)
{
  const out = fixture();
  const packetRow = (id, anchor) => [id, anchor, 'm', 't', 'w', 'e'].join('\t');
  const vf = (batch, rows) => rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tverifier-${batch}\t${HASH}\tverdicts=${rows.length}\n`;
  fs.writeFileSync(path.join(out, 'verifier-packet-b1.tsv'), packetRow('C001', 'a.c:10') + '\n' + packetRow('C002', 'b.c:20') + '\n');
  fs.writeFileSync(path.join(out, 'verifier-packet-b2.tsv'), packetRow('C003', 'c.c:30') + '\n');
  fs.writeFileSync(path.join(out, 'verdicts-b1.tsv'), vf('b1', [
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P1', 't->o', 'b.c:20', '-', '-'],
  ]));
  fs.writeFileSync(path.join(out, 'verdicts-b2.tsv'), vf('b2', [
    ['C003', 'CONFIRMED', 'P1', 'DUPLICATE-OF C001: same mech', 'c.c:30', '-', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('across batches'), r.stderr);
}

// error= in STATUS is surfaced verbatim
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), `STATUS\tverifier-1\t${HASH}\tverdicts=0\terror=packet unreadable\n`);
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('error=packet unreadable'), r.stderr);
}

// CONFIRMED P0 TEST-GAP blocks merge-ready and renders verdict/severity
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P2', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P0', 'TEST-GAP: mutation X still green', 'b.c:20', '-', 'add mutation test'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 0, r.stderr);
  const report = fs.readFileSync(path.join(out, 'report.md'), 'utf8');
  assert.ok(report.includes('**do not merge**'), 'confirmed P0 test-gap must block');
  assert.ok(report.includes('C002 CONFIRMED/P0'), 'test-gap bullet carries verdict/severity');
}

// DUPLICATE-OF after TEST-GAP prefix is rejected (would silently skip dup validation)
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P2', 'TEST-GAP: t->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 'TEST-GAP: DUPLICATE-OF C001: same', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('DUPLICATE-OF must come FIRST'), r.stderr);
}

// absent packets and absent provenance fail loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  fs.rmSync(path.join(out, 'verifier-packet-1.tsv'));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('cannot validate batch ID ownership'), r.stderr);

  const out2 = fixture();
  fs.writeFileSync(path.join(out2, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  fs.rmSync(path.join(out2, 'private', 'provenance.json'));
  const r2 = run(out2);
  assert.strictEqual(r2.code, 1);
  assert.ok(r2.stderr.includes('provenance.json missing'), r2.stderr);
}

// a failing rerun removes the stale report.md from a previous green run
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
    ['C002', 'CONFIRMED', 'P2', 't->o', 'b.c:20', '-', '-'],
    ['C003', 'REFUTED', 'DROP', 't->o', 'c.c:30', 'killed', '-'],
  ]));
  assert.strictEqual(run(out).code, 0);
  assert.ok(fs.existsSync(path.join(out, 'report.md')));
  fs.writeFileSync(path.join(out, 'verdicts-1.tsv'), verdictsFile([
    ['C001', 'CONFIRMED', 'P1', 't->o', 'a.c:10', '-', '-'],
  ]));
  assert.strictEqual(run(out).code, 1);
  assert.ok(!fs.existsSync(path.join(out, 'report.md')), 'stale report.md must not survive a red rerun');
}

console.log('compose-report.test OK');
