// roundtrip.mjs — lossless-capture proof for the parser.
//
// For every .rpt file given: parse -> EpanetReport -> regenerate -> diff
// against the original, byte for byte (no masking: wall-clock stamps are
// stored raw).  Any dropped or mangled field shows up as a difference.
//
// Usage:  node roundtrip.mjs [--dump <dir>] <file-or-dir>...
//         (directories are scanned for *.rpt)
//
// Exit status 0 only when every report is byte-identical AND fully
// recognized (no `unrecognized` events).
import { readFileSync, readdirSync, statSync, writeFileSync, mkdirSync } from 'fs';
import { join, basename } from 'path';
import { parseReport } from './dist/parse.js';
import { regenerateReport } from './dist/regenerate.js';

const args = process.argv.slice(2);
let dumpDir = null;
const inputs = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--dump') dumpDir = args[++i];
  else inputs.push(args[i]);
}
if (!inputs.length) {
  console.error('Usage: node roundtrip.mjs [--dump <dir>] <file-or-dir>...');
  process.exit(2);
}
const files = [];
for (const a of inputs) {
  if (statSync(a).isDirectory()) {
    for (const f of readdirSync(a).sort()) {
      if (f.endsWith('.rpt')) files.push(join(a, f));
    }
  } else files.push(a);
}
if (dumpDir) mkdirSync(dumpDir, { recursive: true });

let identical = 0;
const mismatched = [];
const unrecognizedFiles = [];

for (const file of files) {
  const text = readFileSync(file, 'latin1');
  const { report, unrecognized } = parseReport(text);
  const regen = regenerateReport(report);
  if (dumpDir) writeFileSync(join(dumpDir, basename(file)), regen, 'latin1');

  if (unrecognized.length) {
    unrecognizedFiles.push({ file, lines: unrecognized });
  }
  if (regen === text) {
    identical++;
    continue;
  }
  const a = text.split('\n');
  const b = regen.split('\n');
  let i = 0;
  while (i < a.length && i < b.length && a[i] === b[i]) i++;
  mismatched.push({
    file,
    line: i + 1,
    native: a[i] ?? '<EOF>',
    regen: b[i] ?? '<EOF>',
  });
}

console.log(`round trip: ${files.length} reports, ${identical} byte-identical, ` +
  `${mismatched.length} differ, ${unrecognizedFiles.length} with unrecognized lines`);
for (const m of mismatched.slice(0, 15)) {
  console.log(`\n${m.file} first differs at line ${m.line}`);
  console.log(`  native |${m.native}|`);
  console.log(`  regen  |${m.regen}|`);
}
const shapes = new Map();
for (const u of unrecognizedFiles) {
  for (const l of u.lines) {
    const key = l.trim().replace(/\d+/g, 'N').slice(0, 60);
    shapes.set(key, (shapes.get(key) ?? 0) + 1);
  }
}
if (shapes.size) {
  console.log('\nunrecognized line shapes:');
  [...shapes.entries()].sort((x, y) => y[1] - x[1]).slice(0, 20)
    .forEach(([k, n]) => console.log(String(n).padStart(6), `|${k}|`));
}
process.exit(mismatched.length || unrecognizedFiles.length ? 1 : 0);
