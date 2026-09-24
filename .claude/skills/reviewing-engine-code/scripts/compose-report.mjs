#!/usr/bin/env node
// Stage 5: non-destructive report render. Every candidate lands exactly once;
// fails loud when any candidate lacks a verdict.

import fs from 'node:fs';
import path from 'node:path';
import { parseArtifact, checkStatus } from './tsv.mjs';
import { writeArtifact, readArtifact } from './outdir.mjs';

function arg(name) {
  const i = process.argv.indexOf(`--${name}`);
  if (i < 0) return undefined;
  const v = process.argv[i + 1];
  if (v === undefined || v.startsWith('--')) {
    console.error(`compose-report: --${name} is missing its value`);
    process.exit(2);
  }
  return v;
}

const out = arg('out');
if (!out) {
  console.error('usage: compose-report.mjs --out <dir> [--not-covered <lensid,...>] [--telemetry <file>]');
  process.exit(2);
}

// A stale report.md from a previous green run must not survive a failing rerun —
// its existence is the custody-rule gate for reading private/.
fs.rmSync(path.join(out, 'report.md'), { force: true });

let scope, candidates;
try {
  scope = JSON.parse(readArtifact(out, 'scope.json'));
  candidates = readArtifact(out, 'candidates.tsv')
    .split('\n')
    .filter(Boolean)
    .map((l) => l.split('\t'));
} catch (e) {
  console.error(`compose-report: ${e.message}`);
  process.exit(1);
}

// #region collect verdicts from all verifier batches
const VERDICTS = new Set(['CONFIRMED', 'PLAUSIBLE', 'REFUTED']);
const SEVERITIES = new Set(['P0', 'P1', 'P2', 'DROP']);
const candidateIds = new Set(candidates.map((c) => c[0]));
// Expected batches derive from packet files on disk: a verdict file must match its
// packet's ID set exactly, or a verifier verdicts claims it never saw — the one
// silent-corruption path of deferring validation to this stage.
const packetIds = new Map();
for (const f of fs.readdirSync(out).filter((n) => /^verifier-packet.*\.tsv$/.test(n))) {
  const m = /^verifier-packet-(.+)\.tsv$/.exec(f);
  const ids = readArtifact(out, f).split('\n').filter(Boolean).map((l) => l.split('\t')[0]);
  packetIds.set(m ? m[1] : '', new Set(ids));
}
const DUP_RE = /^DUPLICATE-OF\s+(C\d+):\s*/i;
const verdicts = new Map();
const idBatch = new Map();
const dupes = [];
const errors = [];
if (packetIds.size === 0) {
  errors.push('no verifier-packet*.tsv in OUTPUT — cannot validate batch ID ownership (stage-3/4 packets missing?)');
}
if (candidates.length && !fs.existsSync(path.join(out, 'private', 'provenance.json'))) {
  errors.push('private/provenance.json missing — stage 3 did not complete in this OUTPUT');
}
const verdictFiles = fs.readdirSync(out).filter((n) => /^verdicts-.*\.tsv$/.test(n));
for (const f of verdictFiles) {
  const batch = f.replace(/^verdicts-/, '').replace(/\.tsv$/, '');
  let rows;
  // Errors accumulate per file: one malformed batch must not hide the others'
  // problems, and a raw stacktrace makes useless respawn error lines.
  try {
    const parsed = parseArtifact(readArtifact(out, f), 7, f);
    rows = parsed.rows;
    const s = checkStatus(parsed.status, f, { expectHash: scope.assignment_hash, expectId: `verifier-${batch}`, expectCount: rows.length, countKey: 'verdicts' });
    if (s.kv.error) errors.push(`${f}: verifier reported error=${s.kv.error}`);
  } catch (e) {
    errors.push(e.message);
    continue;
  }
  for (const r of rows) {
    if (!VERDICTS.has(r[1])) errors.push(`${f}: ${r[0]} verdict '${r[1]}' outside CONFIRMED|PLAUSIBLE|REFUTED`);
    if (!SEVERITIES.has(r[2])) errors.push(`${f}: ${r[0]} severity '${r[2]}' outside P0|P1|P2|DROP`);
    if (!candidateIds.has(r[0])) errors.push(`${f}: verdict for unknown candidate id ${r[0]}`);
    if (r[1] === 'REFUTED' && r[5] === '-') errors.push(`${f}: ${r[0]} REFUTED without a killing fact in column 6`);
    if (r[2] === 'DROP' && r[5] === '-') errors.push(`${f}: ${r[0]} severity DROP without a drop reason in column 6`);
    if (/TEST-GAP:\s*DUPLICATE-OF/i.test(r[3])) errors.push(`${f}: ${r[0]} DUPLICATE-OF must come FIRST in trigger->outcome (before TEST-GAP:) or the mark is unvalidated`);
    if (verdicts.has(r[0])) dupes.push(r[0]);
    verdicts.set(r[0], r);
    idBatch.set(r[0], batch);
  }
  const expected = packetIds.get(batch) ?? (packetIds.size === 1 && packetIds.has('') ? packetIds.get('') : null);
  if (expected) {
    const got = new Set(rows.map((r) => r[0]));
    const miss = [...expected].filter((x) => !got.has(x));
    const extra = [...got].filter((x) => !expected.has(x));
    if (miss.length) errors.push(`${f}: missing verdicts for its own packet ids: ${miss.join(',')}`);
    if (extra.length) errors.push(`${f}: verdicts for ids outside its packet: ${extra.join(',')} — this verifier never saw those claim rows`);
  } else if (packetIds.size) {
    errors.push(`${f}: no verifier-packet-${batch}.tsv on disk — stale file from a previous run?`);
  }
}
for (const b of packetIds.keys()) {
  if (b && !verdictFiles.includes(`verdicts-${b}.tsv`)) {
    errors.push(`verifier-packet-${b}.tsv has no verdicts-${b}.tsv — batch never verified, or its file landed outside OUTPUT`);
  }
}
// Verifier-marked duplicates: the primary must exist in the same batch, be unmarked
// itself, and carry the identical verdict+severity — otherwise the mark is noise.
for (const [id, r] of verdicts) {
  const dm = DUP_RE.exec(r[3]);
  if (!dm) continue;
  const at = `verdicts-${idBatch.get(id)}.tsv: ${id}`;
  const t = verdicts.get(dm[1]);
  if (!t) errors.push(`${at}: DUPLICATE-OF ${dm[1]} but no such verdict exists`);
  else if (DUP_RE.test(t[3])) errors.push(`${at}: DUPLICATE-OF ${dm[1]} which is itself marked duplicate — chains are not allowed`);
  else if (t[1] !== r[1] || t[2] !== r[2]) errors.push(`${at}: marked DUPLICATE-OF ${dm[1]} but verdict/severity differ (${r[1]}/${r[2]} vs ${t[1]}/${t[2]})`);
  else if (packetIds.size && idBatch.get(id) !== idBatch.get(dm[1])) errors.push(`${at}: DUPLICATE-OF ${dm[1]} across batches — a verifier can only mark duplicates inside its own packet`);
}
const missing = candidates.map((c) => c[0]).filter((id) => !verdicts.has(id));
if (missing.length || dupes.length || errors.length) {
  if (missing.length) console.error(`compose-report: candidates without a verdict: ${missing.join(',')}`);
  if (dupes.length) console.error(`compose-report: duplicate verdicts: ${dupes.join(',')}`);
  for (const e of errors) console.error(`compose-report: ${e}`);
  console.error('Respawn the offending verifier batch once with these error lines appended (delete its invalid verdicts file first); abort the review on second failure.');
  process.exit(1);
}
// #endregion

// #region partition
const sevRank = { P0: 0, P1: 1, P2: 2, DROP: 3 };
const findings = [];
const testGaps = [];
const refuted = [];
const dropped = [];
const entryById = new Map();
for (const c of candidates) {
  const [id, anchor, mechanism, trigger, wrongOutcome] = c;
  const v = verdicts.get(id);
  const [, verdict, severity, chain, evidence, fact, oracle] = v;
  const dm = DUP_RE.exec(chain);
  entryById.set(id, { id, anchor, mechanism, trigger, wrongOutcome, verdict, severity, chain, evidence, fact, oracle, dupOf: dm ? dm[1] : null, dups: [] });
}
for (const entry of entryById.values()) {
  // Verifier-marked duplicates nest under their primary — visible, never standalone.
  if (entry.dupOf) {
    entryById.get(entry.dupOf).dups.push(entry);
    continue;
  }
  // TEST-GAP routing is the VERIFIER's call (prefix on its trigger->outcome);
  // the lens's mechanism prefix is a hint it can promote or demote.
  if (entry.verdict === 'REFUTED') refuted.push(entry);
  else if (entry.severity === 'DROP') dropped.push(entry);
  else if (/^TEST-GAP:/i.test(entry.chain)) testGaps.push(entry);
  else findings.push(entry);
}
findings.sort((a, b) => (a.verdict === b.verdict ? sevRank[a.severity] - sevRank[b.severity] : a.verdict === 'CONFIRMED' ? -1 : 1));
// #endregion

// Test-gaps keep their verdict/severity weight: a CONFIRMED P0/P1 oracle gap blocks
// merge-ready exactly like an implementation finding.
const blockPool = [...findings, ...testGaps];
const confirmedBlocking = blockPool.some((f) => f.verdict === 'CONFIRMED' && (f.severity === 'P0' || f.severity === 'P1'));
const plausibleBlocking = blockPool.some((f) => f.verdict === 'PLAUSIBLE' && (f.severity === 'P0' || f.severity === 'P1'));
const suggested = confirmedBlocking ? (blockPool.some((f) => f.verdict === 'CONFIRMED' && f.severity === 'P0') ? 'do not merge' : 'merge with fixes') : 'merge-ready';

const L = [];
L.push(`# Review report — ${scope.branch}`);
L.push('');
L.push(`Scope: BASE ${scope.base} ... HEAD ${scope.head} (hash ${scope.assignment_hash})`);
L.push(`Suggested verdict: **${suggested}**${plausibleBlocking ? ' — plausible P0/P1 present: explicit merge risk' : ''}`);
L.push('');
const md = (s) => String(s).replace(/\|/g, '\\|');
const dupList = (e) => e.dups.map((d) => `${d.id} \`${md(d.anchor)}\``).join(', ');
const dupNote = (e) => (e.dups.length ? ` (duplicates: ${dupList(e)})` : '');
const row = (e) => `| ${e.id} | ${e.verdict} | ${e.severity} | \`${md(e.anchor)}\` | ${md(e.mechanism)} | ${md(e.chain)} | ${md(e.evidence)} | ${md(e.fact)} | ${md(e.oracle)} | ${e.dups.length ? dupList(e) : '-'} |`;
L.push(`## Findings (${findings.length})`);
L.push('');
if (findings.length) {
  L.push('| ID | verdict | sev | anchor | mechanism | trigger->outcome | evidence | missing fact | oracle | duplicates |');
  L.push('|---|---|---|---|---|---|---|---|---|---|');
  for (const f of findings) L.push(row(f));
} else L.push('None.');
L.push('');
L.push(`## Standalone test-oracle gaps (${testGaps.length})`);
L.push('');
for (const t of testGaps) L.push(`- ${t.id} ${t.verdict}/${t.severity} \`${md(t.anchor)}\` — ${md(t.chain)} (oracle: ${md(t.oracle)})${dupNote(t)}`);
if (!testGaps.length) L.push('None.');
L.push('');
L.push(`## Challenged candidates (${refuted.length}) — killing fact cited, nothing silently deleted`);
L.push('');
for (const r of refuted) L.push(`- ${r.id} \`${md(r.anchor)}\` — ${md(r.wrongOutcome)} — KILLED BY: ${md(r.fact)}${dupNote(r)}`);
if (!refuted.length) L.push('None.');
L.push('');
L.push(`## Dropped (${dropped.length})`);
L.push('');
for (const d of dropped) L.push(`- ${d.id} \`${md(d.anchor)}\` — ${md(d.fact)}${dupNote(d)}`);
if (!dropped.length) L.push('None.');
L.push('');
L.push('## Footer');
L.push('');
const notCovered = (arg('not-covered') || '').split(',').filter(Boolean);
// NOT-COVERED claims must name genuinely routed lenses, not fiction.
try {
  const routing = parseArtifact(readArtifact(out, 'routing.tsv'), 4, 'routing.tsv');
  const routed = new Set(routing.rows.filter((x) => x[0] === 'LENS').map((x) => x[1]));
  const bogus = notCovered.filter((id) => !routed.has(id));
  if (bogus.length) {
    console.error(`compose-report: --not-covered names unrouted lens(es): ${bogus.join(',')}`);
    process.exit(1);
  }
} catch (e) {
  // Absence is tolerable only when there is nothing to validate: a corrupt
  // routing.tsv must not let fictional NOT-COVERED lens ids into the footer.
  if (notCovered.length) {
    console.error(`compose-report: cannot validate --not-covered: ${e.message}`);
    process.exit(1);
  }
}
L.push(`- Lenses NOT-COVERED after respawn: ${notCovered.length ? notCovered.join(', ') : 'none'}`);
// A fully-suppressed lens must be visible: render per-lens QID disposition summary.
const provPath = path.join(out, 'private', 'provenance.json');
if (fs.existsSync(provPath)) {
  let prov;
  try {
    prov = JSON.parse(fs.readFileSync(provPath, 'utf8'));
  } catch (e) {
    console.error(`compose-report: private/provenance.json is corrupt: ${e.message}`);
    process.exit(1);
  }
  const disp = prov.qid_dispositions || {};
  L.push('- Question dispositions per lens:');
  for (const [lens, qids] of Object.entries(disp)) {
    const vals = Object.values(qids);
    const nCand = vals.filter((v) => v !== 'clear' && !v.startsWith('blocked:')).length;
    const nClear = vals.filter((v) => v === 'clear').length;
    const nBlocked = vals.length - nCand - nClear;
    L.push(`  - ${lens}: ${vals.length} QIDs — ${nCand} with candidates, ${nClear} clear, ${nBlocked} blocked${vals.length > 0 && nClear === vals.length ? ' (ALL CLEAR — check independently)' : ''}`);
  }
}
const telemetryFile = arg('telemetry');
if (telemetryFile) {
  let tl;
  try {
    tl = fs.readFileSync(telemetryFile, 'utf8');
  } catch (e) {
    console.error(`compose-report: cannot read --telemetry file: ${e.message}`);
    process.exit(1);
  }
  L.push('- Telemetry (harness totals; exact nc is post-hoc JSONL work):');
  for (const line of tl.split('\n').filter(Boolean)) L.push(`  - ${line}`);
}
L.push(`- Repo ${scope.repo}, branch ${scope.branch}`);
L.push('');

writeArtifact(out, 'report.md', L.join('\n'));
console.log(`report.md: ${findings.length} findings, ${testGaps.length} test-gaps, ${refuted.length} challenged, ${dropped.length} dropped; suggested: ${suggested}`);
