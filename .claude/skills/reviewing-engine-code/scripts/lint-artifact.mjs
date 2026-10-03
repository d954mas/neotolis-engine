#!/usr/bin/env node
// Self-lint for role artifacts, run by the AUTHOR agent on its OWN file before
// STATUS. Purpose: convert full respawns (~100k+ tokens each) into in-session
// fixes. Mirrors the fatal checks of pack-candidates/compose-report that a
// single agent can violate alone; cross-artifact checks stay downstream.

import fs from 'node:fs';
import { parseArtifact, checkStatus } from './tsv.mjs';

const args = {};
for (let i = 3; i < process.argv.length; i += 2) {
  if (!process.argv[i].startsWith('--')) usage(`unexpected arg '${process.argv[i]}'`);
  args[process.argv[i].slice(2)] = process.argv[i + 1];
}
const mode = process.argv[2];
function usage(msg) {
  console.error(`lint-artifact: ${msg}`);
  console.error('usage: lint-artifact.mjs lens --file <lens-*.tsv> --id <lensid> --hash <hash> [--qids Q1,Q2] [--inventory <inventory.json>]');
  console.error('       lint-artifact.mjs verdicts --file <verdicts-*.tsv> --batch <name> --hash <hash> --packet <verifier-packet-*.tsv>');
  process.exit(2);
}
if (mode !== 'lens' && mode !== 'verdicts') usage(`unknown mode '${mode}'`);
if (!args.file) usage('--file is required');
if (!fs.existsSync(args.file)) { console.error(`lint-artifact: file not found: ${args.file}`); process.exit(1); }

const errors = [];
const warnings = [];
const err = (m) => errors.push(m);
const name = args.file.replace(/\\/g, '/').split('/').pop();
const QID_RE = /^Q\d+$/;
const ANCHOR_RE = /^(.+):(\d+)$/;
const MAX_FIELD = 700;

let parsed = null;
try {
  parsed = parseArtifact(fs.readFileSync(args.file, 'utf8'), 7, name);
} catch (e) {
  // parseArtifact fails first-throw; surface it and stop — later checks need rows.
  console.error(`lint-artifact: ${e.message}`);
  console.error('FIX AND RERUN — do not emit STATUS while this lint fails.');
  process.exit(1);
}
const { rows, status } = parsed;
for (const r of rows) for (const f of r) if (f.length > MAX_FIELD) err(`row ${r[0]}: field exceeds ${MAX_FIELD} chars`);

// #region lens mode
if (mode === 'lens') {
  if (!args.id || !args.hash) usage('lens mode needs --id and --hash');
  const cands = rows.filter((r) => !QID_RE.test(r[0]));
  const disps = rows.filter((r) => QID_RE.test(r[0]));
  try {
    checkStatus(status, name, { expectHash: args.hash, expectId: args.id, expectCount: cands.length, countKey: 'rows' });
  } catch (e) { err(e.message); }

  const candIds = cands.map((r) => r[0]);
  const candIdSet = new Set(candIds);
  for (const d of candIds.filter((x, i) => candIds.indexOf(x) !== i)) err(`duplicate candidate id ${d}`);
  for (const r of cands) {
    const m = ANCHOR_RE.exec(r[1]);
    if (!m) err(`candidate ${r[0]} anchor '${r[1]}' is not file:line`);
    else if (Number(m[2]) < 1) err(`candidate ${r[0]} anchor line < 1`);
  }
  for (const r of disps) {
    const d = r[1];
    if (d !== 'clear' && !d.startsWith('blocked:') && !d.split(',').every((c) => candIdSet.has(c))) {
      err(`disposition ${r[0]}='${d}' names unknown candidate ids`);
    }
    for (let i = 2; i < 7; i++) if (r[i] !== '-') err(`disposition ${r[0]} column ${i + 1} must be '-'`);
  }
  if (args.qids) {
    const want = new Set(args.qids.split(',').filter(Boolean));
    const have = new Set(disps.map((r) => r[0]));
    for (const q of want) if (!have.has(q)) err(`assigned ${q} has no disposition row`);
    for (const q of have) if (!want.has(q)) err(`disposition for ${q} which was not assigned to you`);
  }
  if (args.inventory) {
    try {
      const inv = JSON.parse(fs.readFileSync(args.inventory, 'utf8'));
      const invPaths = new Set(inv.map((e) => e.path));
      const allowed = new Set([...invPaths, ...inv.filter((e) => e.old_path).map((e) => e.old_path)]);
      const s = checkStatus(status, name, {});
      const readList = (s.kv.read || 'none').split(',').filter((p) => p && p !== 'none');
      const unreadList = (s.kv.unread || 'none').split(',').filter((p) => p && p !== 'none');
      const both = new Set([...readList, ...unreadList]);
      for (const p of both) if (!allowed.has(p)) err(`'${p}' in read/unread is not an inventory path`);
      for (const p of invPaths) if (!both.has(p)) err(`inventory path '${p}' missing from read/unread partition`);
      for (const p of readList) if (unreadList.includes(p)) err(`'${p}' listed both read and unread`);
    } catch (e) { err(`inventory check failed: ${e.message}`); }
  }
}
// #endregion

// #region verdicts mode
if (mode === 'verdicts') {
  if (!args.batch || !args.hash || !args.packet) usage('verdicts mode needs --batch, --hash, --packet');
  const VERDICTS = new Set(['CONFIRMED', 'PLAUSIBLE', 'REFUTED']);
  const SEVERITIES = new Set(['P0', 'P1', 'P2', 'DROP']);
  const DUP_RE = /^DUPLICATE-OF (\S+): /;
  try {
    checkStatus(status, name, { expectHash: args.hash, expectId: `verifier-${args.batch}`, expectCount: rows.length, countKey: 'verdicts' });
  } catch (e) { err(e.message); }

  const packetIds = new Set(
    fs.readFileSync(args.packet, 'utf8').split(/\r?\n/).filter(Boolean).map((l) => l.split('\t')[0]),
  );
  const byId = new Map();
  for (const r of rows) {
    if (byId.has(r[0])) err(`duplicate verdict for ${r[0]}`);
    byId.set(r[0], r);
    if (!packetIds.has(r[0])) err(`verdict for ${r[0]} which is not in your packet`);
    if (!VERDICTS.has(r[1])) err(`${r[0]}: verdict '${r[1]}' outside CONFIRMED|PLAUSIBLE|REFUTED`);
    if (!SEVERITIES.has(r[2])) err(`${r[0]}: severity '${r[2]}' outside P0|P1|P2|DROP`);
    if (r[1] === 'REFUTED' && r[5] === '-') err(`${r[0]}: REFUTED without a killing fact in column 6`);
    if (r[2] === 'DROP' && r[5] === '-') err(`${r[0]}: severity DROP without a drop reason in column 6`);
    if (/TEST-GAP:\s*DUPLICATE-OF/i.test(r[3])) err(`${r[0]}: DUPLICATE-OF must come FIRST (before TEST-GAP:)`);
  }
  for (const r of rows) {
    const dm = DUP_RE.exec(r[3]);
    if (!dm) continue;
    const t = byId.get(dm[1]);
    if (!t) err(`${r[0]}: DUPLICATE-OF ${dm[1]} but no such verdict in your file`);
    else if (DUP_RE.test(t[3])) err(`${r[0]}: DUPLICATE-OF ${dm[1]} which is itself marked duplicate`);
    else if (t[1] !== r[1] || t[2] !== r[2] || t[5] !== r[5]) err(`${r[0]}: DUPLICATE-OF ${dm[1]} but verdict/severity/column-6 differ`);
  }
  // Missing packet IDs are a WARNING, not an error: a Write cut by the output
  // ceiling cannot be fixed by rewriting (same ceiling) — the pipeline's
  // truncation split owns that case. Only rewrite if you actually SKIPPED rows.
  const missing = [...packetIds].filter((id) => !byId.has(id));
  if (missing.length) warnings.push(`missing verdicts for packet ids: ${missing.join(',')} — rewrite ONLY if you skipped them; if your Write was cut mid-file, finish with your honest STATUS instead`);
}
// #endregion

for (const w of warnings) console.error(`lint-artifact: WARNING ${w}`);
if (errors.length) {
  console.error(`lint-artifact: ${name} FAILED:`);
  for (const m of errors) console.error(`  - ${m}`);
  console.error('FIX AND RERUN — do not emit STATUS while this lint fails.');
  process.exit(1);
}
console.log(`lint-artifact: ${name} OK (${rows.length} rows)`);
