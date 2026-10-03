import assert from 'node:assert';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));
const script = path.join(here, 'check-mapper.mjs');
const HASH = 'abc123def456';

function fixture() {
  const out = fs.mkdtempSync(path.join(os.tmpdir(), 'v13-mapper-'));
  fs.writeFileSync(path.join(out, 'scope.json'), JSON.stringify({ assignment_hash: HASH }));
  fs.writeFileSync(path.join(out, 'routing-hint.tsv'),
    'HINT\tarchitecture\tpermanent\nHINT\tbehavior-contracts\tpermanent\nFORCE\tuntrusted-input\tdeps-boundary\n');
  return out;
}
const qFile = (rows) => rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tmapper\t${HASH}\tquestions=${rows.length}\n`;
const rFile = (rows, lensCount) => rows.map((r) => r.join('\t')).join('\n') + '\n' + `STATUS\tmapper\t${HASH}\tlenses=${lensCount}\tdrops=0\trules=present\n`;
const run = (out) => {
  try {
    return { code: 0, stdout: execFileSync('node', [script, '--out', out], { encoding: 'utf8' }) };
  } catch (e) {
    return { code: e.status, stderr: String(e.stderr) };
  }
};
const QS = [
  ['Q01', 'architecture,behavior-contracts', 'op A', 'What exact result does A produce for inputs x, y, z?'],
  ['Q02', 'untrusted-input', 'op B admission', 'Which values are accepted, narrowed, rejected at B?'],
];
const LENSES5 = [
  ['LENS', 'architecture', 'Q01', 'contract stmt arch'],
  ['LENS', 'behavior-contracts', 'Q01', 'contract stmt behavior'],
  ['LENS', 'untrusted-input', 'Q02', 'contract stmt input'],
  ['LENS', 'state-data-lifecycle', 'Q01', 'contract stmt state'],
  ['LENS', 'test-oracle', 'Q02', 'contract stmt tests'],
];

// happy path incl. canonical lens list output
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS));
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5, 5));
  const r = run(out);
  assert.strictEqual(r.code, 0, r.stderr);
  assert.ok(r.stdout.includes('lenses=architecture,behavior-contracts,untrusted-input,state-data-lifecycle,test-oracle'));
}

// orphan QID fails
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile([...QS, ['Q03', 'concurrency', 'op C', 'What sequence does C run?']]));
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5, 5));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('orphan question Q03'), r.stderr);
}

// missing FORCE lens fails
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS));
  const noForce = LENSES5.filter((l) => l[1] !== 'untrusted-input');
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile([...noForce, ['LENS', 'concurrency', 'Q02', 'stmt']], 5));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes(`FORCE lens 'untrusted-input' is not routed`), r.stderr);
}

// floor violation fails
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS));
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5.slice(0, 3), 3));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('floor 5'), r.stderr);
}

// DROP of a FORCE lens fails; second packet id '-2' passes vocabulary
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS));
  const rows = [...LENSES5.map((l) => (l[1] === 'untrusted-input' ? ['LENS', 'untrusted-input', 'Q02', 'stmt'] : l)),
    ['LENS', 'behavior-contracts-2', 'Q02', 'second packet stmt'],
    ['DROP', 'untrusted-input', '-', 'no changed path or symbol in the frozen inventory exercises this class']];
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(rows.filter((r2) => r2[0] === 'LENS' || r2[0] === 'DROP'), 6));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes(`DROP of FORCE lens 'untrusted-input'`), r.stderr);
  assert.ok(!r.stderr.includes(`unknown lens id 'behavior-contracts-2'`), '-2 suffix must be legal');
}

// malformed QID shape fails (stage 3 would misparse dispositions)
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile([...QS, ['Q1a', 'concurrency', 'op', 'What sequence runs?']]));
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5, 5));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes("QID 'Q1a' does not match"), r.stderr);
}

// drops= count mismatch fails
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS));
  const body = LENSES5.map((r2) => r2.join('\t')).join('\n') + '\n' +
    `STATUS\tmapper\t${HASH}\tlenses=5\tdrops=2\trules=present\n`;
  fs.writeFileSync(path.join(out, 'routing.tsv'), body);
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('drops=2 but file has 0'), r.stderr);
}

// truncated file (no STATUS) fails loud
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), QS.map((r) => r.join('\t')).join('\n') + '\n');
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5, 5));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('missing STATUS'), r.stderr);
}

// hash mismatch fails
{
  const out = fixture();
  fs.writeFileSync(path.join(out, 'questions.tsv'), qFile(QS).replace(HASH, 'deadbeef0000'));
  fs.writeFileSync(path.join(out, 'routing.tsv'), rFile(LENSES5, 5));
  const r = run(out);
  assert.strictEqual(r.code, 1);
  assert.ok(r.stderr.includes('wrong scope'), r.stderr);
}

console.log('check-mapper.test OK');
