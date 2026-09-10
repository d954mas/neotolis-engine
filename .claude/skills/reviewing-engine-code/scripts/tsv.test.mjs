import assert from 'node:assert';
import { formatRow, parseArtifact, checkStatus } from './tsv.mjs';

const throws = (fn, re, msg) => assert.throws(fn, (e) => re.test(e.message), msg);

// formatRow
assert.strictEqual(formatRow(['a', 'b:1', 'c']), 'a\tb:1\tc');
throws(() => formatRow(['a\tb']), /raw tab/);
throws(() => formatRow(['a\nb']), /raw tab|newline/);
throws(() => formatRow(['a' + String.fromCharCode(0)]), /control character/);

// parseArtifact
const ok = 'C1\tf.c:10\tmech\ttrig\twrong\tev\t-\nC2\tg.c:20\tm2\tt2\tw2\te2\tfact\nSTATUS\tlens-a\thash12\trows=2\tread=f.c\tunread=none\n';
const { rows, status } = parseArtifact(ok, 7, 't');
assert.strictEqual(rows.length, 2);
assert.strictEqual(status[0], 'STATUS');
throws(() => parseArtifact('C1\tf.c:10\tmech\ttrig\twrong\tev\t-\n', 7, 't'), /missing STATUS/);
throws(() => parseArtifact('C1\tonly\tthree\nSTATUS\ta\tb\trows=1\n', 7, 't'), /expected 7 fields/);
throws(() => parseArtifact('STATUS\ta\tb\trows=0\nC1\tf\tm\tt\tw\te\t-\n', 7, 't'), /not the last row/);
throws(() => parseArtifact('C1\tf.c:1\t\ttrig\tw\te\t-\nSTATUS\ta\tb\trows=1\n', 7, 't'), /empty field/);
throws(() => parseArtifact('', 7, 't'), /empty file/);

// checkStatus
const st = ['STATUS', 'lens-a', 'hash12', 'rows=2', 'read=f.c,g.c', 'unread=none'];
const s = checkStatus(st, 't', { expectHash: 'hash12', expectCount: 2, countKey: 'rows' });
assert.strictEqual(s.kv.read, 'f.c,g.c');
throws(() => checkStatus(st, 't', { expectHash: 'other', countKey: 'rows' }), /wrong scope/);
throws(() => checkStatus(st, 't', { expectHash: 'hash12', expectCount: 3, countKey: 'rows' }), /truncated/);
throws(() => checkStatus(['STATUS', 'x'], 't', {}), /malformed STATUS/);
throws(() => checkStatus(['STATUS', 'a', 'h', 'read=a.c', 'unread=none', 'read=none'], 't', {}), /duplicate STATUS key/);
throws(() => checkStatus(st, 't', { expectHash: 'hash12', expectId: 'other', countKey: 'rows' }), /cross-pasted/);

console.log('tsv.test OK');
