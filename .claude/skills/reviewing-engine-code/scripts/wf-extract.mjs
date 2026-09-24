#!/usr/bin/env node
// H77 workflow helper: deterministic reads of v13 OUTPUT artifacts, printed as JSON
// for the orchestrating Workflow script (which has no filesystem access itself).
// Standalone on purpose — never imports v13 scripts, never writes into v13's dir.
// The only write is `split` authoring a new batch-map JSON (a sanctioned hand-edit
// per v13 stage 4; here it is script-authored for determinism).

import fs from 'node:fs';
import path from 'node:path';

function fail(msg) {
  console.error(`wf-extract: ${msg}`);
  process.exit(1);
}

function arg(name) {
  const i = process.argv.indexOf(`--${name}`);
  if (i < 0) return undefined;
  const v = process.argv[i + 1];
  if (v === undefined || v.startsWith('--')) fail(`--${name} needs a value`);
  return v;
}

const cmd = process.argv[2];
const out = arg('out');
if (!cmd || !out) fail('usage: wf-extract.mjs <freshcheck|scope|mapper|batches|classify|split> --out <dir> [--batch <name>] [--map <json>]');

// #region freshcheck — marker-based so the freeze CHAIN stays idempotent on retry
if (cmd === 'freshcheck') {
  const marker = path.join(out, '.h77-run');
  if (!fs.existsSync(out)) {
    fs.mkdirSync(out, { recursive: true });
    fs.writeFileSync(marker, '');
    process.exit(0);
  }
  if (fs.existsSync(marker)) process.exit(0); // retry of this run, not a stale review
  if (fs.readdirSync(out).length === 0) {
    fs.writeFileSync(marker, '');
    process.exit(0);
  }
  fail(`OUTPUT dir ${out} is not fresh (stale artifacts from a prior review, no .h77-run marker)`);
}
// #endregion

if (!fs.existsSync(out)) fail(`OUTPUT dir not found: ${out}`);

const readText = (name) => {
  const p = path.join(out, name);
  if (!fs.existsSync(p)) fail(`missing artifact ${name}`);
  return fs.readFileSync(p, 'utf8');
};
// /\r?\n/ mirrors tsv.mjs — a plain '\n' split turns CRLF blank lines into phantom rows.
const rows = (text) => text.split(/\r?\n/).filter(Boolean).map((l) => l.split('\t'));
const emit = (obj) => process.stdout.write(JSON.stringify(obj) + '\n');

// #region scope
if (cmd === 'scope') {
  const s = JSON.parse(readText('scope.json'));
  emit({
    repo: s.repo, base: s.base, head: s.head, branch: s.branch,
    assignment_hash: s.assignment_hash, function_context: s.function_context,
    agents_base: s.agents_base,
    // freeze-scope copies it only when the reviewed checkout has one (stderr NOTE otherwise)
    principle_catalog: fs.existsSync(path.join(out, 'principle-catalog.md')),
  });
  process.exit(0);
}
// #endregion

// #region mapper — per-lens routing + verbatim QID rows (post-check-mapper only)
if (cmd === 'mapper') {
  const qLines = readText('questions.tsv').split('\n').filter(Boolean);
  const qByQid = new Map();
  for (const line of qLines) {
    const qid = line.split('\t')[0];
    if (qid !== 'STATUS') qByQid.set(qid, line);
  }
  const lenses = [];
  const drops = [];
  for (const r of rows(readText('routing.tsv'))) {
    if (r[0] === 'LENS') {
      const qids = (r[2] || '').split(',').filter((x) => x && x !== '-');
      const missing = qids.filter((q) => !qByQid.has(q));
      if (missing.length) fail(`routing.tsv lens ${r[1]} references unknown QIDs ${missing.join(',')} — run check-mapper first`);
      lenses.push({ id: r[1], contract: r[3] || '-', qids, qid_rows: qids.map((q) => qByQid.get(q)) });
    } else if (r[0] === 'DROP') {
      drops.push({ id: r[1], reason: r[3] || '-' });
    }
  }
  if (!lenses.length) fail('routing.tsv has no LENS rows');
  emit({ lenses, drops });
  process.exit(0);
}
// #endregion

// #region batches — batch-map.json + packet existence check
if (cmd === 'batches') {
  const map = JSON.parse(readText('batch-map.json'));
  const batches = [];
  for (const [name, ids] of Object.entries(map)) {
    const packet = `verifier-packet-${name}.tsv`;
    if (!fs.existsSync(path.join(out, packet))) fail(`batch-map.json names '${name}' but ${packet} is missing`);
    if (!Array.isArray(ids) || !ids.length) fail(`batch '${name}' has an empty ID list`);
    batches.push({ name, packet, ids, rows: ids.length });
  }
  emit({ batches });
  process.exit(0);
}
// #endregion

const batch = arg('batch');

// #region classify — verdict-file state for the stage-5 failure taxonomy
if (cmd === 'classify') {
  if (!batch) fail('classify needs --batch');
  const packetRows = rows(readText(`verifier-packet-${batch}.tsv`));
  const packetIds = new Set(packetRows.map((r) => r[0]));
  const vPath = path.join(out, `verdicts-${batch}.tsv`);
  if (!fs.existsSync(vPath)) {
    emit({ state: 'missing' });
    process.exit(0);
  }
  const vRows = rows(fs.readFileSync(vPath, 'utf8'));
  const statusIdx = vRows.findIndex((r) => r[0] === 'STATUS');
  const statusRow = statusIdx >= 0 ? vRows[statusIdx] : null;
  const verdictIds = vRows.filter((r) => r[0] !== 'STATUS').map((r) => r[0]);
  const unknown = verdictIds.filter((id) => !packetIds.has(id));
  const dupes = verdictIds.filter((id, i) => verdictIds.indexOf(id) !== i);
  const missing = [...packetIds].filter((id) => !verdictIds.includes(id));
  const statusLast = statusIdx === vRows.length - 1 && statusIdx >= 0;
  const errKv = statusRow ? statusRow.slice(3).find((p) => p.startsWith('error=') && p.length > 6) : undefined;
  // Precedence: a declared-error stub is the taxonomy's respawn-with-reason branch;
  // a below-packet ID set is truncation even when the cut mangled the last ID into
  // an 'unknown' (same-size retry is forbidden for truncation); only a full-coverage
  // file with alien/duplicate IDs is malformed.
  let state;
  let reason;
  if (verdictIds.length === 0 && errKv) {
    state = 'error';
    reason = errKv.slice('error='.length);
  } else if (!statusLast || missing.length) state = 'truncated';
  else if (unknown.length || dupes.length) state = 'malformed';
  else state = 'present';
  emit({ state, reason, missing, unknown, dupes, status_present: statusIdx >= 0 });
  process.exit(0);
}
// #endregion

// #region split — halve a truncated batch per the v13 stage-4 rule
if (cmd === 'split') {
  if (!batch) fail('split needs --batch');
  const mapPath = arg('map') ? arg('map') : path.join(out, 'batch-map.json');
  const map = JSON.parse(fs.readFileSync(mapPath, 'utf8'));
  if (!map[batch]) fail(`batch '${batch}' not in ${mapPath}`);
  const packetRows = rows(readText(`verifier-packet-${batch}.tsv`));
  if (packetRows.length < 2) fail(`batch '${batch}' has ${packetRows.length} row(s) — nothing to split`);
  const anchors = packetRows.map((r) => {
    const m = /^(.*):(\d+)$/.exec(r[1] || '');
    if (!m) fail(`packet row ${r[0]} has malformed anchor '${r[1]}'`);
    return { id: r[0], file: m[1], line: Number(m[2]) };
  });

  // Split at the file boundary nearest to half; but a lopsided boundary (an
  // FFD batch is often one 44-row run + a 1-row filler) defeats the split's
  // purpose — the big side re-truncates and the split-child guard aborts. Fall
  // back to the largest anchor-line gap between same-file neighbors, which is
  // also the single-file rule.
  const half = anchors.length / 2;
  let cut = -1;
  const boundaries = [];
  for (let i = 1; i < anchors.length; i++) if (anchors[i].file !== anchors[i - 1].file) boundaries.push(i);
  if (boundaries.length) {
    const bCut = boundaries.reduce((best, i) => (Math.abs(i - half) < Math.abs(best - half) ? i : best), boundaries[0]);
    if (Math.min(bCut, anchors.length - bCut) >= anchors.length / 3) cut = bCut;
  }
  if (cut === -1) {
    let bestGap = -1;
    for (let i = 1; i < anchors.length; i++) {
      if (anchors[i].file !== anchors[i - 1].file) continue;
      const gap = anchors[i].line - anchors[i - 1].line;
      if (gap > bestGap || (gap === bestGap && Math.abs(i - half) < Math.abs(cut - half))) {
        bestGap = gap;
        cut = i;
      }
    }
    // Every adjacent pair crosses a file: the boundary cut is the only cut there is.
    if (cut === -1) cut = boundaries.reduce((best, i) => (Math.abs(i - half) < Math.abs(best - half) ? i : best), boundaries[0]);
  }
  const subA = `${batch}a`;
  const subB = `${batch}b`;
  if (map[subA] || map[subB]) fail(`sub-batch name collision: ${subA}/${subB} already in the map`);
  const newMap = {};
  for (const [name, ids] of Object.entries(map)) {
    if (name !== batch) {
      newMap[name] = ids;
    } else {
      newMap[subA] = anchors.slice(0, cut).map((a) => a.id);
      newMap[subB] = anchors.slice(cut).map((a) => a.id);
    }
  }
  const mapFile = path.join(out, `batch-map.split-${batch}.json`);
  fs.writeFileSync(mapFile, JSON.stringify(newMap, null, 2));
  emit({
    map_file: mapFile,
    sub_batches: [subA, subB],
    keep: Object.keys(map).filter((n) => n !== batch),
  });
  process.exit(0);
}
// #endregion

fail(`unknown command '${cmd}'`);
