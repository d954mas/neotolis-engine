#!/usr/bin/env node
// Optional lens accelerator: print N numbered file ranges in one call.
// Usage: node read-ranges.mjs <file>:<start>-<end> [...more]
// Never constrains what a lens may read — it only batches chosen reads.

import fs from 'node:fs';

const specs = process.argv.slice(2);
if (specs.length === 0) {
  console.error('usage: read-ranges.mjs <file>:<start>-<end> [...]');
  process.exit(2);
}

for (const spec of specs) {
  const m = /^(.*):(\d+)-(\d+)$/.exec(spec);
  if (!m) {
    console.error(`bad range spec: ${spec}`);
    process.exit(2);
  }
  const [, file, a, b] = m;
  const start = Number(a);
  const end = Number(b);
  let text;
  try {
    text = fs.readFileSync(file, 'utf8');
  } catch (e) {
    console.log(`== ${spec} ERROR ${e.code || e.message}`);
    continue;
  }
  const lines = text.split(/\r?\n/);
  console.log(`== ${file}:${start}-${Math.min(end, lines.length)}`);
  for (let i = start; i <= end && i <= lines.length; i++) {
    console.log(`${i}\t${lines[i - 1]}`);
  }
}
