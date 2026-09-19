#!/usr/bin/env node
// Stage 3: validate lens artifacts, union candidates, enforce the coverage ledger,
// emit the fact-only verifier packet. Private provenance never enters the packet.
// Design rule after the G-audit: checks that cannot run must ABORT, never skip —
// a tripwire with a silent-open mode is worse than no tripwire.

import path from 'node:path';
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import { parseArtifact, checkStatus, formatRow } from './tsv.mjs';
import { resolveOutDir, writeArtifact, readArtifact } from './outdir.mjs';

const VERSION = 'pack-candidates v13.1';
const QID_RE = /^Q\d+$/;
const ANCHOR_RE = /^([^:\s]+):(\d+)$/;
const BATCH_RE = /^[a-z0-9][a-z0-9_-]*$/;
const HEDGE_RE = /\b(may|might|possibly|probably|likely|unverified|unclear|perhaps)\b/i;
const MAX_FIELD = 700;

function arg(name) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 ? process.argv[i + 1] : undefined;
}
const flag = (name) => process.argv.includes(`--${name}`);

const outArg = arg('out');
if (!outArg) {
  console.error('usage: pack-candidates.mjs --out <dir> [--expect <lensid,...> --allow-partial] [--auto-batch <N> | --batch-map <json>] [--keep-verdicts <batch,...>] [--not-covered <lensid,...>]');
  process.exit(2);
}

const scopeRaw = JSON.parse(readArtifact(outArg, 'scope.json'));
const out = resolveOutDir(outArg, scopeRaw.repo || process.cwd());
const scope = scopeRaw;
const inventory = JSON.parse(readArtifact(out, 'inventory.json'));

// #region environment probe — repo checks run verified or the stage aborts
const git = (...a) => execFileSync('git', ['-C', scope.repo, ...a], { encoding: 'utf8', maxBuffer: 1 << 26, stdio: ['ignore', 'pipe', 'ignore'] });
let repoChecks = false;
if (scope.repo) {
  try {
    git('rev-parse', '--git-dir');
    const head = git('rev-parse', 'HEAD').trim();
    if (scope.head && head !== scope.head) {
      console.error(`pack-candidates: scope.repo HEAD ${head.slice(0, 10)} != scope.head ${String(scope.head).slice(0, 10)} — wrong checkout, abort`);
      process.exit(1);
    }
    repoChecks = true;
  } catch (e) {
    console.error(`pack-candidates: repo checks UNAVAILABLE (${e.message.split('\n')[0]}) — refusing to proceed blind`);
    process.exit(1);
  }
  const dirty = git('status', '--porcelain', '--untracked-files=no').trim();
  if (dirty) {
    console.error('pack-candidates: reviewed tree became DIRTY during discovery — evidence is poisoned, abort:');
    console.error(dirty);
    process.exit(1);
  }
} else {
  console.error('pack-candidates: NOTICE — scope.repo unset, repo-dependent checks disabled (test fixtures only)');
}
const fileLines = new Map();
function headFileLines(file) {
  if (!fileLines.has(file)) {
    try {
      const text = git('show', `${scope.head || 'HEAD'}:${file}`);
      fileLines.set(file, text.replace(/\n$/, '').split('\n'));
    } catch {
      fileLines.set(file, null); // absent at HEAD — an ERROR for anchors, never a skip
    }
  }
  return fileLines.get(file);
}
// #endregion

// #region expected lens set — routing.tsv is authoritative
let expect = (arg('expect') || '').split(',').filter(Boolean);
const routedQids = new Map();
let routedIds = null;
try {
  const r = parseArtifact(readArtifact(out, 'routing.tsv'), 4, 'routing.tsv');
  const lensRows = r.rows.filter((x) => x[0] === 'LENS');
  routedIds = lensRows.map((x) => x[1]);
  for (const [, id, qids] of lensRows) routedQids.set(id, qids.split(',').filter((q) => q && q !== '-'));
} catch {
  /* routing.tsv absent: test mode, --expect required */
}
// Sanctioned NOT-COVERED continuation: a twice-failed lens is excluded here and
// surfaced by compose-report; ids must be genuinely routed.
const notCovered = (arg('not-covered') || '').split(',').filter(Boolean);
if (routedIds) {
  for (const id of notCovered) {
    if (!routedIds.includes(id)) {
      console.error(`pack-candidates: --not-covered '${id}' is not a routed lens`);
      process.exit(1);
    }
  }
  if (expect.length === 0) expect = routedIds.filter((id) => !notCovered.includes(id));
  else {
    const a = new Set(expect), b = new Set(routedIds.filter((id) => !notCovered.includes(id)));
    const equal = a.size === b.size && [...a].every((x) => b.has(x));
    if (!equal && !flag('allow-partial')) {
      console.error(`pack-candidates: --expect (${[...a].join(',')}) != routed set (${routedIds.join(',')}) — a typo here silently discards review coverage; pass --allow-partial only if you mean it`);
      process.exit(1);
    }
    if (!equal) console.error(`pack-candidates: WARNING --allow-partial: packing ${a.size} of ${b.size} routed lenses`);
  }
} else if (expect.length === 0) {
  console.error('pack-candidates: no routing.tsv and no --expect — nothing to pack');
  process.exit(1);
} else {
  console.error('pack-candidates: NOTICE — routing.tsv absent, expected set from --expect (test mode)');
}
// #endregion

// #region validate lens artifacts (all errors accumulate; nothing fails first-throw)
const invPaths = new Set(inventory.map((e) => e.path));
const partitionAllowed = new Set([...invPaths, ...inventory.filter((e) => e.old_path).map((e) => e.old_path)]);
const perLens = new Map();
const dispositions = {};
const readSet = new Set();
const errors = [];
const hedges = [];
for (const id of expect) {
  const name = `lens-${id}.tsv`;
  let parsed;
  try {
    // A --batch-map rerun happens AFTER the mandated mv into private/ — accept either home.
    let text;
    try {
      text = readArtifact(out, name);
    } catch {
      text = readArtifact(path.join(out, 'private'), name);
    }
    parsed = parseArtifact(text, 7, name);
  } catch (e) {
    errors.push(e.message.includes('missing artifact') ? `missing artifact lens-${id}.tsv (checked OUTPUT and OUTPUT/private) — if this lens failed twice, rerun with --not-covered ${id}` : e.message);
    continue;
  }
  const err = (m) => errors.push(`${name}: ${m}`);
  const { rows, status } = parsed;
  const cands = rows.filter((r) => !QID_RE.test(r[0]));
  const disps = rows.filter((r) => QID_RE.test(r[0]));
  let s = null;
  try {
    s = checkStatus(status, name, { expectHash: scope.assignment_hash, expectId: id, expectCount: cands.length, countKey: 'rows' });
  } catch (e) {
    errors.push(e.message);
  }

  for (const r of rows) {
    for (const f of r) if (f.length > MAX_FIELD) err(`row ${r[0]}: field exceeds ${MAX_FIELD} chars`);
  }

  if (s) {
    const readList = (s.kv.read || 'none').split(',').filter((p) => p && p !== 'none');
    const unreadList = (s.kv.unread || 'none').split(',').filter((p) => p && p !== 'none');
    const both = new Set([...readList, ...unreadList]);
    for (const p of both) if (!partitionAllowed.has(p)) err(`'${p}' in read/unread is not an inventory path (old_path of a rename is allowed)`);
    for (const p of invPaths) if (!both.has(p)) err(`inventory path '${p}' missing from read/unread partition`);
    for (const p of readList) if (unreadList.includes(p)) err(`'${p}' listed both read and unread`);
    for (const p of readList) if (invPaths.has(p)) readSet.add(p);
  }

  const candIds = cands.map((r) => r[0]);
  const candIdSet = new Set(candIds);
  if (candIdSet.size !== candIds.length) {
    err(`duplicate candidate ids: ${candIds.filter((x, i) => candIds.indexOf(x) !== i).join(',')}`);
  }
  for (const r of disps) {
    if (ANCHOR_RE.test(r[1])) err(`row ${r[0]} looks like a candidate but its id matches Q<digits> — candidate ids must not look like QIDs`);
  }
  for (const r of cands) {
    const m = ANCHOR_RE.exec(r[1]);
    if (!m) {
      err(`candidate ${r[0]} anchor '${r[1]}' is not file:line (ranges/prose are invalid)`);
      continue;
    }
    const lineNo = Number(m[2]);
    if (lineNo < 1) err(`candidate ${r[0]} anchor line ${lineNo} < 1`);
    if (repoChecks) {
      const lines = headFileLines(m[1]);
      if (lines === null) {
        err(`candidate ${r[0]} anchor file '${m[1]}' does not exist at HEAD — anchor the causal HEAD line (deleted-file defects anchor at the surviving consumer/insertion site)`);
      } else if (lineNo > lines.length) {
        err(`candidate ${r[0]} anchors ${r[1]} past EOF (${lines.length} lines at HEAD) — diff-offset coordinates?`);
      } else if (lines[lineNo - 1].trim() === '') {
        err(`candidate ${r[0]} anchors a blank line (${r[1]}) — anchor the causal construct`);
      }
    }
    for (const f of [r[2], r[3], r[4], r[5]]) {
      if (HEDGE_RE.test(f)) hedges.push(`${name} ${r[0]}: '${f.slice(0, 80)}'`);
    }
  }
  const assigned = routedQids.get(id);
  const seen = new Set(disps.map((r) => r[0]));
  if (assigned) {
    for (const q of assigned) if (!seen.has(q)) err(`assigned ${q} has no disposition row (candidates|clear|blocked:<fact>)`);
    for (const q of seen) if (!assigned.includes(q)) err(`disposition for ${q} which was not assigned to this lens (cross-pasted?)`);
  }
  for (const r of disps) {
    const d = r[1];
    if (d !== 'clear' && !d.startsWith('blocked:') && !d.split(',').every((c) => candIdSet.has(c))) {
      err(`disposition ${r[0]}='${d}' names unknown candidate ids`);
    }
  }
  perLens.set(id, cands);
  dispositions[id] = Object.fromEntries(disps.map((r) => [r[0], r[1]]));
}
if (errors.length) {
  console.error('pack-candidates: lens artifact validation failed:');
  for (const e of errors) console.error(`  - ${e}`);
  console.error('Respawn each offending lens once with ONLY its own error lines appended.');
  process.exit(1);
}
// #endregion

// #region coverage ledger — hard fail on a changed file no lens read
const unreadByAll = inventory.map((e) => e.path).filter((p) => !readSet.has(p));
if (unreadByAll.length) {
  console.error('pack-candidates: COVERAGE FAIL — changed files unread by every lens:');
  for (const p of unreadByAll) console.error(`  - ${p}`);
  console.error('Route one additional lens at the gap and rerun this stage.');
  process.exit(1);
}
// #endregion

// #region union + dedup (same file:line AND same normalized wrong_outcome only)
const norm = (s) => s.toLowerCase().replace(/\s+/g, ' ').trim();
// Sort by anchor so same-mechanism duplicates sit adjacent in the verifier packet —
// one shared file read proves them together instead of once per batch.
const flat = [];
for (const [lensId, rows] of perLens) for (const row of rows) flat.push({ lensId, row });
flat.sort((a, b) => {
  const pa = a.row[1].split(':'), pb = b.row[1].split(':');
  return pa[0] === pb[0] ? Number(pa[1]) - Number(pb[1]) : pa[0] < pb[0] ? -1 : 1;
});
const candidates = [];
const provenance = {};
const byKey = new Map();
let seq = 0;
for (const { lensId, row } of flat) {
  const [origId, anchor, mechanism, trigger, wrongOutcome, evidence, missingFact] = row;
  const key = `${anchor}::${norm(wrongOutcome)}`;
  if (byKey.has(key)) {
    const cid = byKey.get(key);
    provenance[cid].sources.push({ lens: lensId, id: origId, row });
    // On merge keep the strongest ROW (by citation length), not the routing-order winner.
    const packed = candidates.find((c) => c[0] === cid);
    if (evidence.length > packed[5].length) {
      packed[2] = mechanism;
      packed[3] = trigger;
      packed[5] = evidence;
    }
    continue;
  }
  const cid = `C${String(++seq).padStart(3, '0')}`;
  byKey.set(key, cid);
  candidates.push([cid, anchor, mechanism, trigger, wrongOutcome, evidence]);
  provenance[cid] = { missing_fact: missingFact, sources: [{ lens: lensId, id: origId, row }] };
}
// #endregion

// Private artifacts live OUTSIDE the verifier's named inputs; SKILL forbids reading
// private/ (any agent, incl. the conductor) until report.md exists.
const privDir = path.join(out, 'private');
fs.mkdirSync(privDir, { recursive: true });
writeArtifact(out, 'candidates.tsv', candidates.map((c) => formatRow(c, 'candidates')).join('\n') + '\n');
writeArtifact(privDir, 'provenance.json', JSON.stringify({
  version: VERSION,
  coverage_read: [...readSet].sort(),
  qid_dispositions: dispositions,
  hedge_warnings: hedges,
  candidates: provenance,
}, null, 2));
if (hedges.length) console.error(`pack-candidates: NOTE ${hedges.length} hedge-worded field(s) reached the packet (list in private/provenance.json) — facts should state the unproven condition as a trigger, not a doubt`);

// #region auto-batch — mechanical split against the verifier output ceiling
// A verdict file is ONE Write call in one model message; measured ceiling ~11.8k
// output tokens (a 139-row batch truncated at 111). Batches close on anchor-file
// boundaries so same-anchor clusters are never split.
function buildAutoBatches(cands, target) {
  const allowance = Math.ceil((target * 4) / 3);
  const runs = [];
  for (const c of cands) {
    const file = c[1].split(':')[0];
    if (runs.length && runs[runs.length - 1].file === file) runs[runs.length - 1].rows.push(c);
    else runs.push({ file, rows: [c] });
  }
  const units = [];
  for (const r of runs) {
    let parts = [r.rows];
    while (parts.some((p) => p.length > allowance)) {
      const next = [];
      for (const p of parts) {
        if (p.length <= allowance) {
          next.push(p);
          continue;
        }
        let gapAt = 1, gap = -1;
        for (let i = 1; i < p.length; i++) {
          const g = Number(p[i][1].split(':')[1]) - Number(p[i - 1][1].split(':')[1]);
          if (g > gap) { gap = g; gapAt = i; }
        }
        console.error(`pack-candidates: WARNING --auto-batch splits single-file run '${r.file}' before ${p[gapAt][1]} (${p.length} rows > allowance ${allowance})`);
        next.push(p.slice(0, gapAt), p.slice(gapAt));
      }
      parts = next;
    }
    units.push(...parts);
  }
  // First-fit-decreasing: order-preserving greedy left tiny tail batches whose
  // fixed spawn cost (~82k) dwarfs their rows. Units are whole file runs; packing
  // them out of anchor order is safe — a batch spans multiple files anyway.
  const batches = [];
  for (const u of [...units].sort((a, b) => b.length - a.length)) {
    let placed = false;
    if (u.length <= target) {
      for (const b of batches) {
        if (b.length + u.length <= target) { b.push(...u); placed = true; break; }
      }
    }
    if (!placed) batches.push([...u]);
  }
  const order = new Map(cands.map((c, i) => [c[0], i]));
  for (const b of batches) b.sort((x, y) => order.get(x[0]) - order.get(y[0]));
  batches.sort((x, y) => order.get(x[0][0]) - order.get(y[0][0]));
  const m = {};
  batches.forEach((b, i) => {
    if (b.length > target) console.error(`pack-candidates: NOTICE oversized batch b${i + 1}: ${b.length} rows (single-file run within allowance ${allowance})`);
    m[`b${i + 1}`] = b.map((c) => c[0]);
  });
  return m;
}
// #endregion

// #region fact-only packet (+ batches)
// Order matters: EVERYTHING is validated before anything is deleted — verdict files
// are verifier output (not derivable), so a typo in --batch-map/--keep-verdicts must
// cost an error message, never a green batch. Only then are stale packets/verdicts
// removed (stage 5 globs both; leftovers would attach verdicts to dead claims).
const keepVerdicts = (arg('keep-verdicts') || '').split(',').filter(Boolean);
const batchMapPath = arg('batch-map');
const autoBatch = arg('auto-batch');
if (batchMapPath && autoBatch !== undefined) {
  console.error('pack-candidates: --auto-batch and --batch-map are mutually exclusive');
  process.exit(1);
}
let map = null;
if (batchMapPath) {
  try {
    map = JSON.parse(fs.readFileSync(batchMapPath, 'utf8'));
  } catch (e) {
    console.error(`pack-candidates: cannot read --batch-map: ${e.message}`);
    process.exit(1);
  }
} else if (autoBatch !== undefined) {
  const target = Number(autoBatch);
  if (!Number.isInteger(target) || target < 1) {
    console.error(`pack-candidates: --auto-batch '${autoBatch}' is not a positive integer`);
    process.exit(1);
  }
  map = buildAutoBatches(candidates, target);
}
if (keepVerdicts.length && !map) {
  console.error(`pack-candidates: --keep-verdicts requires a batch map (--batch-map or --auto-batch)`);
  process.exit(1);
}
if (map) {
  const assigned = new Set();
  for (const [batch, ids] of Object.entries(map)) {
    if (!BATCH_RE.test(batch)) {
      console.error(`pack-candidates: illegal batch name '${batch}' (lowercase letters/digits/_/- only — case collisions corrupt packets on NTFS)`);
      process.exit(1);
    }
    if (!Array.isArray(ids) || ids.length === 0) {
      console.error(`pack-candidates: batch '${batch}' is empty — a verifier spawn on nothing`);
      process.exit(1);
    }
    for (const cid of ids) {
      if (assigned.has(cid)) {
        console.error(`pack-candidates: candidate ${cid} appears in more than one batch — it would be verified and verdicted twice`);
        process.exit(1);
      }
      assigned.add(cid);
    }
  }
  const missingIds = candidates.filter((c) => !assigned.has(c[0])).map((c) => c[0]);
  if (missingIds.length) {
    console.error(`pack-candidates: batch map misses candidates: ${missingIds.join(',')}`);
    process.exit(1);
  }
  for (const b of keepVerdicts) {
    if (!map[b]) {
      console.error(`pack-candidates: --keep-verdicts '${b}' has no batch of that name in the new map`);
      process.exit(1);
    }
    let kept;
    try {
      kept = parseArtifact(readArtifact(out, `verdicts-${b}.tsv`), 7, `verdicts-${b}.tsv`);
    } catch (e) {
      console.error(`pack-candidates: --keep-verdicts '${b}': ${e.message}`);
      process.exit(1);
    }
    const gotIds = new Set(kept.rows.map((r) => r[0]));
    const wantIds = new Set(map[b]);
    const same = gotIds.size === wantIds.size && [...wantIds].every((x) => gotIds.has(x));
    if (!same) {
      console.error(`pack-candidates: kept verdicts-${b}.tsv ID set != batch '${b}' ID set — the kept file verdicts different claims; delete it or fix the map`);
      process.exit(1);
    }
  }
}
for (const f of fs.readdirSync(out).filter((n) => /^verifier-packet.*\.tsv$/.test(n))) {
  fs.rmSync(path.join(out, f));
}
for (const f of fs.readdirSync(out).filter((n) => /^verdicts-.*\.tsv$/.test(n))) {
  const b = f.replace(/^verdicts-/, '').replace(/\.tsv$/, '');
  if (!keepVerdicts.includes(b)) {
    console.error(`pack-candidates: NOTICE — removing stale ${f} (not in --keep-verdicts)`);
    fs.rmSync(path.join(out, f));
  }
}
if (map) {
  for (const [batch, ids] of Object.entries(map)) {
    const rows = candidates.filter((c) => ids.includes(c[0]));
    writeArtifact(out, `verifier-packet-${batch}.tsv`, rows.map((c) => formatRow(c, 'packet')).join('\n') + '\n');
    console.log(`pack-candidates: batch ${batch}: ${rows.length} rows (${rows[0][0]}..${rows[rows.length - 1][0]})`);
  }
  // Public artifact: derivable from candidates.tsv; remainder-respawn bookkeeping.
  writeArtifact(out, 'batch-map.json', JSON.stringify(map, null, 2));
} else {
  writeArtifact(out, 'verifier-packet.tsv', candidates.map((c) => formatRow(c, 'packet')).join('\n') + '\n');
}
// #endregion

const contributed = [...perLens.entries()].map(([id, r]) => `${id}:${r.length}`).join(' ');
const ncNote = notCovered.length ? `; NOT-COVERED: ${notCovered.join(',')}` : '';
console.log(`${VERSION}: packed ${candidates.length} candidates from ${perLens.size} lenses (${contributed}); dedup merged ${flat.length - candidates.length}; coverage OK (${readSet.size} files)${ncNote}`);
