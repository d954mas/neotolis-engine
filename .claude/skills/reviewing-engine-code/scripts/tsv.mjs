// Shared strict TSV layer — the ONLY parser/writer for V13 artifacts.
// Bans raw NUL/control chars, enforces exact field counts, fails loud.

const CTRL_RE = /[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/;

export function assertCleanField(field, where) {
  if (CTRL_RE.test(field)) {
    throw new Error(`${where}: control character in field (write literal \\t/\\n/\\0 as spaces)`);
  }
}

export function formatRow(fields, where = 'row') {
  for (const f of fields) {
    if (typeof f !== 'string') throw new Error(`${where}: non-string field`);
    if (f.includes('\t') || f.includes('\n')) throw new Error(`${where}: raw tab/newline in field`);
    assertCleanField(f, where);
  }
  return fields.join('\t');
}

// Parses a lens/verifier artifact: data rows with exactly `fieldCount` fields,
// terminated by one STATUS row. Returns { rows, status }.
export function parseArtifact(text, fieldCount, name) {
  const lines = text.split(/\r?\n/).filter((l) => l.length > 0);
  if (lines.length === 0) throw new Error(`${name}: empty file`);
  const rows = [];
  let status = null;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    assertCleanField(line, `${name}:${i + 1}`);
    const fields = line.split('\t');
    if (fields[0] === 'STATUS') {
      if (i !== lines.length - 1) throw new Error(`${name}: STATUS row is not the last row`);
      status = fields;
      continue;
    }
    if (fields.length !== fieldCount) {
      throw new Error(`${name}:${i + 1}: expected ${fieldCount} fields, got ${fields.length}`);
    }
    if (fields.some((f) => f.trim() === '')) {
      throw new Error(`${name}:${i + 1}: empty field (use '-' for none)`);
    }
    rows.push(fields);
  }
  if (!status) throw new Error(`${name}: missing STATUS row (truncated output?)`);
  return { rows, status };
}

// STATUS<TAB>id<TAB>hash<TAB>k=v...  -> { id, hash, kv }
export function parseStatus(fields, name) {
  if (fields.length < 3) throw new Error(`${name}: malformed STATUS row`);
  const kv = {};
  for (const part of fields.slice(3)) {
    const eq = part.indexOf('=');
    if (eq <= 0) throw new Error(`${name}: malformed STATUS pair '${part}'`);
    const k = part.slice(0, eq);
    // read= feeds the coverage ledger: a duplicated key must never be last-wins.
    if (k in kv) throw new Error(`${name}: duplicate STATUS key '${k}'`);
    kv[k] = part.slice(eq + 1);
  }
  return { id: fields[1], hash: fields[2], kv };
}

export function checkStatus(status, name, { expectHash, expectCount, countKey, expectId }) {
  const s = parseStatus(status, name);
  if (expectHash && s.hash !== expectHash) {
    throw new Error(`${name}: STATUS hash '${s.hash}' != assignment hash '${expectHash}' (wrong scope?)`);
  }
  if (expectId && s.id !== expectId) {
    throw new Error(`${name}: STATUS id '${s.id}' != expected '${expectId}' (cross-pasted artifact?)`);
  }
  if (countKey !== undefined) {
    const n = Number(s.kv[countKey]);
    if (!Number.isInteger(n)) throw new Error(`${name}: STATUS missing ${countKey}=<n>`);
    if (expectCount !== undefined && n !== expectCount) {
      throw new Error(`${name}: STATUS ${countKey}=${n} but file has ${expectCount} rows (truncated?)`);
    }
  }
  return s;
}
