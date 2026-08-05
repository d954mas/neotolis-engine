// Shared OUTPUT-dir policy: containment outside the repo + one overwrite rule
// (atomic tmp+rename; stage reruns overwrite their own artifacts, never others').

import fs from 'node:fs';
import path from 'node:path';

export function resolveOutDir(out, repoRoot) {
  if (!out) throw new Error('--out is required');
  const abs = path.resolve(out);
  const repoAbs = path.resolve(repoRoot);
  const rel = path.relative(repoAbs, abs);
  if (rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel))) {
    throw new Error(`--out ${abs} is inside the repository ${repoAbs}; OUTPUT must be external`);
  }
  fs.mkdirSync(abs, { recursive: true });
  return abs;
}

export function writeArtifact(dir, name, content) {
  const target = path.join(dir, name);
  const tmp = `${target}.tmp-${process.pid}`;
  fs.writeFileSync(tmp, content);
  fs.renameSync(tmp, target);
  return target;
}

export function readArtifact(dir, name) {
  const target = path.join(dir, name);
  if (!fs.existsSync(target)) throw new Error(`missing artifact ${target}`);
  return fs.readFileSync(target, 'utf8');
}
