// selftest.mjs — checks that no engine-run corpus can:
//
// 1. Parser/regenerator symmetry for the status-line shapes no test
//    network triggers (FMT56 setting change — dead code in 2.3-dev but
//    kept defensively; FMT61 valve ill-conditioning; FMT62 system
//    ill-conditioned and its no-trailing-blank warning grammar; the
//    flow-change-for-Node convergence detail without a head-error line).
//    A hand-built EpanetReport is regenerated, reparsed, regenerated
//    again: bytes must match and the reparsed model must deep-equal the
//    original.  The sequences follow src/report.c writehyderr() /
//    src/hydsolver.c badvalve() — source-derived, not engine-produced.
//
// 2. Streaming equivalence: feeding a real report through
//    ReportParser.write() one byte at a time must produce the same
//    model as a single write.
//
// Usage:  node selftest.mjs [<real-report-for-streaming-check>.rpt]
import { readFileSync, writeFileSync, mkdirSync } from 'fs';
import { ReportParser, ReportReducer, parseReport } from './dist/parse.js';
import { regenerateReport } from './dist/regenerate.js';

const header = {
  version: { major: 2, minor: 3, patch: 6 },
  dateStamp: 'Mon Aug 31 00:00:00 2026',
};

/** @type {import('./dist/epanet-report.js').EpanetReport} */
const model = {
  header,
  diagnostics: [
    {
      kind: 'terminal',
      code: 110,
      text: 'Error 110: cannot solve network hydraulic equations',
      beforeAnalysis: false,
    },
  ],
  analysisBegun: 'Mon Aug 31 00:00:00 2026',
  statusLog: [
    {
      time: 0,
      events: [
        { kind: 'balancing-start' },
        { kind: 'trial', trial: 1, relativeFlowChange: 1.0 },
        {
          kind: 'link-switch', linkType: 'PRV', linkId: 'V1',
          from: 'active', to: 'open',
        },
        {
          kind: 'setting-change', linkType: 'FCV', linkId: 'V2',
          setting: 40.0,
        },
        { kind: 'valve-ill-conditioning', valveId: 'V9' },
        { kind: 'trial', trial: 2, relativeFlowChange: 0.5 },
        {
          kind: 'convergence-detail', measure: 'max-flow-change',
          elementType: 'node', elementId: 'N1', value: 0.01,
        },
        { kind: 'ill-conditioned', nodeId: 'J77' },
        {
          kind: 'link-status', linkType: 'Pipe', linkId: 'P1',
          to: 'temporarily closed',
        },
        {
          kind: 'warning',
          warning: { kind: 'node-disconnected', nodeId: 'J77' },
        },
      ],
    },
  ],
  analysisEnded: null,
  complete: false,
};

let failures = 0;
const fail = (msg) => { failures++; console.error('FAIL:', msg); };

// ---- 1. synthetic unreachable-arm symmetry ------------------------
{
  const text = regenerateReport(model);
  mkdirSync('fixtures', { recursive: true });
  writeFileSync('fixtures/synthetic.rpt', text, 'latin1');

  const { report, unrecognized } = parseReport(text);
  if (unrecognized.length) {
    fail(`synthetic: ${unrecognized.length} unrecognized lines: ` +
      JSON.stringify(unrecognized));
  }
  const text2 = regenerateReport(report);
  if (text2 !== text) {
    const a = text.split('\n'); const b = text2.split('\n');
    let i = 0; while (a[i] === b[i]) i++;
    fail(`synthetic: regen not byte-stable at line ${i + 1}: ` +
      `|${a[i]}| vs |${b[i]}|`);
  }
  const [ja, jb] = [JSON.stringify(model), JSON.stringify(report)];
  if (ja !== jb) {
    fail('synthetic: reparsed model differs from original');
    console.error('  original:', ja.slice(0, 400));
    console.error('  reparsed:', jb.slice(0, 400));
  }
}

// ---- 2. byte-at-a-time streaming equivalence ----------------------
{
  const file = process.argv[2] ?? 'fixtures/synthetic.rpt';
  const text = readFileSync(file, 'latin1');
  const oneShot = parseReport(text).report;

  const reducer = new ReportReducer();
  const parser = new ReportParser((ev) => reducer.handle(ev));
  for (const ch of text) parser.write(ch);
  parser.end();

  if (JSON.stringify(oneShot) !== JSON.stringify(reducer.report)) {
    fail(`streaming: byte-at-a-time parse of ${file} differs from one-shot`);
  }
}

if (failures) process.exit(1);
console.log('selftest: synthetic unreachable-arm symmetry + streaming OK');
