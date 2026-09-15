#!/usr/bin/env node
// Stage 1.5: mechanical validation of mapper artifacts — the routing authority
// gets the same distrust as every other agent. Fails loud; prints canonical lens ids.

import { parseArtifact, checkStatus } from './tsv.mjs';
import { readArtifact } from './outdir.mjs';

const VOCAB = [
  'architecture', 'behavior-contracts', 'state-data-lifecycle', 'abi-layout',
  'validation-failure-policy', 'performance-size', 'platform-toolchain',
  'test-oracle', 'concurrency', 'untrusted-input',
];
const PERMANENT = ['architecture', 'behavior-contracts'];
const MAX_FIELD = 700;
const FLOOR = 5;

function arg(name) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? process.argv[i + 1] : undefined;
}
const out = arg('out');
if (!out) {
  console.error('usage: check-mapper.mjs --out <dir>');
  process.exit(2);
}

const errors = [];
const err = (m) => errors.push(m);
// Second packet of one reference is `<id>-2`; base id must be in the menu.
const baseId = (id) => id.replace(/-2$/, '');

try {
  const scope = JSON.parse(readArtifact(out, 'scope.json'));
  const hash = scope.assignment_hash;

  const hints = readArtifact(out, 'routing-hint.tsv').split('\n').filter(Boolean).map((l) => l.split('\t'));
  const forced = hints.filter((h) => h[0] === 'FORCE').map((h) => h[1]);
  const hinted = hints.filter((h) => h[0] === 'HINT').map((h) => h[1]);

  const q = parseArtifact(readArtifact(out, 'questions.tsv'), 4, 'questions.tsv');
  checkStatus(q.status, 'questions.tsv', { expectHash: hash, expectId: 'mapper', expectCount: q.rows.length, countKey: 'questions' });
  const r = parseArtifact(readArtifact(out, 'routing.tsv'), 4, 'routing.tsv');
  const lensRows = r.rows.filter((x) => x[0] === 'LENS');
  const dropRows = r.rows.filter((x) => x[0] === 'DROP');
  const rs = checkStatus(r.status, 'routing.tsv', { expectHash: hash, expectId: 'mapper', expectCount: lensRows.length, countKey: 'lenses' });
  if (Number(rs.kv.drops) !== dropRows.length) err(`routing.tsv: STATUS drops=${rs.kv.drops} but file has ${dropRows.length} DROP rows`);
  if (scope.agents_base !== undefined) {
    const expectRules = scope.agents_base ? 'present' : 'absent';
    if (rs.kv.rules !== expectRules) err(`routing.tsv: STATUS rules=${rs.kv.rules} but scope says agents_base=${scope.agents_base}`);
  }
  for (const x of r.rows) if (x[0] !== 'LENS' && x[0] !== 'DROP') err(`routing.tsv: unknown row type '${x[0]}'`);

  for (const rows of [q.rows, r.rows]) {
    for (const row of rows) {
      for (const f of row) if (f.length > MAX_FIELD) err(`field exceeds ${MAX_FIELD} chars: '${f.slice(0, 60)}...'`);
    }
  }

  // #region QID referential integrity
  const qids = new Set();
  for (const [qid] of q.rows) {
    // The Q\d+ shape is load-bearing at stage 3 (it partitions lens rows) — enforce here.
    if (!/^Q\d+$/.test(qid)) err(`QID '${qid}' does not match Q<digits> — stage 3 would misparse every lens's disposition rows`);
    if (qids.has(qid)) err(`duplicate QID ${qid}`);
    qids.add(qid);
  }
  const routedQids = new Set();
  for (const [, id, qidList] of lensRows) {
    for (const qid of qidList.split(',').filter((x) => x && x !== '-')) {
      if (!qids.has(qid)) err(`routing.tsv: lens ${id} references unknown ${qid}`);
      routedQids.add(qid);
    }
  }
  for (const qid of qids) if (!routedQids.has(qid)) err(`orphan question ${qid}: routed to no lens`);
  // #endregion

  // #region lens vocabulary, permanents, FORCE, floor, DROP legality
  const lensIds = lensRows.map((x) => x[1]);
  const lensBases = new Set(lensIds.map(baseId));
  for (const id of lensIds) {
    if (!VOCAB.includes(baseId(id))) err(`unknown lens id '${id}' (vocabulary: bare names, '-2' for a second packet)`);
  }
  const dupIds = lensIds.filter((id, i) => lensIds.indexOf(id) !== i);
  for (const d of dupIds) err(`duplicate lens id '${d}' (use '${d}-2' for a second packet)`);
  for (const p of PERMANENT) if (!lensBases.has(p)) err(`permanent lens '${p}' is not routed`);
  for (const f of forced) if (!lensBases.has(f)) err(`FORCE lens '${f}' is not routed`);
  if (lensRows.length < FLOOR) err(`only ${lensRows.length} lenses routed (floor ${FLOOR})`);
  for (const [, id, , reason] of dropRows) {
    if (!VOCAB.includes(baseId(id))) err(`DROP of unknown lens '${id}'`);
    if (PERMANENT.includes(baseId(id))) err(`DROP of permanent lens '${id}'`);
    if (forced.includes(baseId(id))) err(`DROP of FORCE lens '${id}'`);
    if (lensBases.has(baseId(id))) err(`lens '${id}' both routed and dropped`);
    if (reason === '-' || reason.trim().length < 20) err(`DROP '${id}': reason missing or too thin`);
  }
  void hinted;
  // #endregion

  if (errors.length) throw new Error('validation failed');
  console.log(`mapper artifacts OK: ${q.rows.length} questions, ${lensRows.length} lenses, ${dropRows.length} drops`);
  console.log(`lenses=${lensIds.join(',')}`);
} catch (e) {
  if (e.message !== 'validation failed') errors.push(e.message);
  console.error('check-mapper: FAILED');
  for (const m of errors) console.error(`  - ${m}`);
  console.error('Respawn the mapper once with the identical prompt; abort the review on second failure.');
  process.exit(1);
}
