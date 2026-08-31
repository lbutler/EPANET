/**
 * regenerate.ts — renders an `EpanetReport` back to EPANET's classic
 * .rpt text, byte-for-byte identical to the engine's own output for the
 * fixed profile (STATUS FULL, SUMMARY NO, PAGE 0, NODES/LINKS NONE,
 * ENERGY NO, MESSAGES YES).
 *
 * This exists as the lossless-capture proof for the parser: parse a
 * native report, regenerate it, and diff — any field the parser dropped
 * or mangled shows up as a byte difference.  Format strings are copied
 * from src/text.h / src/report.c (FMTnn / WARNnn); layout mechanics
 * mirror writeline() with PageSize = 0 (no pagination).
 *
 * Blank lines are not stored in the model — they are re-derived here
 * from the same rules the engine uses:
 *   - FMT64 ("Balancing the network:") and FMT68 (head-error detail)
 *     carry a trailing '\n' in text.h, producing a 0-width blank;
 *   - writehydstat() ends every step block with writeline(" ");
 *   - writehydwarn() adds one writeline(" ") after its warnings —
 *     but the writehyderr() path (ill-conditioned) does not;
 *   - input2.c echoes the offending line with its '\n' still attached
 *     (a 0-width blank follows); ruleerrmsg() echoes stripped text;
 *   - ctime() stamps end with '\n' (blank after Analysis begun/ended).
 */

import type {
  EpanetReport,
  HydraulicTimeStep,
  ReportDiagnostic,
  Seconds,
  SimulationWarning,
  StatusEvent,
} from './epanet-report.js';

/* ------------------------------------------------------------------ */
/*  C-format helpers                                                  */
/* ------------------------------------------------------------------ */

/** clocktime() — `%01d:%02d:%02d`, hours unpadded and unbounded. */
function clock(seconds: Seconds): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds - 3600 * h - 60 * m;
  return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

/** `%.Pf`, preserving printf's "-0.00" for negative zero. */
function fix(x: number, p: number): string {
  const s = x.toFixed(p);
  return Object.is(x, -0) && !s.startsWith('-') ? `-${s}` : s;
}

/** `%W.Pf` (right-aligned in W columns). */
const fixw = (x: number, w: number, p: number): string =>
  fix(x, p).padStart(w);

/** `%12.5e` — C exponent always has at least two digits. */
function e125(x: number): string {
  const s = x.toExponential(5);
  const m = /^(-?\d\.\d+e[+-])(\d+)$/.exec(s);
  const out = m ? m[1] + m[2].padStart(2, '0') : s;
  return out.padStart(12);
}

/* ------------------------------------------------------------------ */
/*  Text constants (src/text.h)                                       */
/* ------------------------------------------------------------------ */

const FMT18 = '  Page 1                                    ';
const LOGO1 = '******************************************************************';
const LOGO2 = '*                           E P A N E T                          *';
const LOGO3 = '*                   Hydraulic and Water Quality                  *';
const LOGO4 = '*                   Analysis for Pipe Networks                   *';
const LOGO6 = LOGO1;

/* ------------------------------------------------------------------ */
/*  Renderer                                                          */
/* ------------------------------------------------------------------ */

export function regenerateReport(r: EpanetReport): string {
  // Each entry is one writeline() payload: rendered as '\n  ' + s, so a
  // trailing '\n' inside s yields the engine's 0-width blank line.
  const lines: string[] = [];
  const w = (s: string): void => { lines.push(s); };

  // ---- Page-1 stamp + logo (writelogo) ----------------------------
  w(LOGO1);
  w(LOGO2);
  w(LOGO3);
  w(LOGO4);
  const v = r.header.version;
  w(`*                          Version ${v.major}.${v.minor}.` +
    `${String(v.patch).padStart(2, '0')}                        *`);
  w(LOGO6);
  w('');

  // ---- pre-run diagnostics (validation / parse / open problems) ---
  for (const d of r.diagnostics) if (d.beforeAnalysis) diagnostic(w, d);

  if (r.analysisBegun !== null) {
    w(`Analysis begun ${r.analysisBegun}\n`);

    // ---- hydraulic status stream (writeheader STATHDR + solver) ---
    w(' ');
    w('Hydraulic Status:');
    w('-'.repeat(71));
    for (const step of r.statusLog) statusStep(w, step);

    // ---- closing balance blocks -----------------------------------
    if (r.flowBalance) {
      const b = r.flowBalance;
      w(`Hydraulic Flow Balance (${b.units})`);
      w('='.repeat(32));
      w(`Total Inflow:      ${fixw(b.totalInflow, 12, 3)}`);
      w(`Consumer Demand:   ${fixw(b.consumerDemand, 12, 3)}`);
      w(`Demand Deficit:    ${fixw(b.demandDeficit, 12, 3)}`);
      w(`Emitter Flow:      ${fixw(b.emitterFlow, 12, 3)}`);
      w(`Leakage Flow:      ${fixw(b.leakageFlow, 12, 3)}`);
      w(`Total Outflow:     ${fixw(b.totalOutflow, 12, 3)}`);
      w(`Storage Flow:      ${fixw(b.storageFlow, 12, 3)}`);
      w(`Flow Ratio:        ${fixw(b.flowRatio, 12, 3)}`);
      w('='.repeat(32) + '\n');
    }
    if (r.massBalance) {
      const b = r.massBalance;
      const suffix = b.unitsSuffix === null ? '' : ` (${b.unitsSuffix})`;
      w(`Water Quality Mass Balance${suffix}`);
      w('='.repeat(32));
      w(`Initial Mass:      ${e125(b.initialMass)}`);
      w(`Mass Inflow:       ${e125(b.massInflow)}`);
      w(`Mass Outflow:      ${e125(b.massOutflow)}`);
      w(`Mass Reacted:      ${e125(b.massReacted)}`);
      w(`Final Mass:        ${e125(b.finalMass)}`);
      w(`Mass Ratio:         ${fix(b.massRatio, 5)}`);
      w(`Total Segments:     ${b.totalSegments}`);
      w('='.repeat(32) + '\n');
    }

    if (r.analysisEnded !== null) w(`Analysis ended ${r.analysisEnded}\n`);
  }

  // ---- terminal diagnostics on a run that started -----------------
  for (const d of r.diagnostics) if (!d.beforeAnalysis) diagnostic(w, d);

  return FMT18 + r.header.dateStamp + '\n' +
    lines.map((s) => '\n  ' + s).join('');
}

/* ------------------------------------------------------------------ */

function diagnostic(w: (s: string) => void, d: ReportDiagnostic): void {
  w(d.text);
  if (d.offendingLine !== undefined) {
    // input2.c echoes the line with its newline still attached.
    w(d.offendingLine + (d.kind === 'parse' ? '\n' : ''));
  }
}

function statusStep(w: (s: string) => void, step: HydraulicTimeStep): void {
  // Warnings always trail a step (writehydstat before writehydwarn).
  let wStart = step.events.length;
  while (wStart > 0 && step.events[wStart - 1].kind === 'warning') wStart--;

  for (let i = 0; i < wStart; i++) statusLine(w, step.events[i], step.time);
  w(' '); // writehydstat() always closes the block

  if (wStart < step.events.length) {
    for (let i = wStart; i < step.events.length; i++) {
      statusLine(w, step.events[i], step.time);
    }
    // writehydwarn() closes its warnings; the ill-conditioned path
    // (writehyderr -> disconnected) does not.
    const failed = step.events.some((e) => e.kind === 'ill-conditioned');
    if (!failed) w(' ');
  }
}

function statusLine(
  w: (s: string) => void,
  e: StatusEvent,
  time: Seconds,
): void {
  const t10 = clock(time).padStart(10);
  switch (e.kind) {
    case 'control-action':
      w(e.cause.kind === 'timer'
        ? `${t10}: ${e.linkType} ${e.linkId} changed by timer control`
        : `${t10}: ${e.linkType} ${e.linkId} changed by ` +
          `${e.cause.nodeType} ${e.cause.nodeId} control`);
      break;
    case 'rule-action':
      w(`${t10}: ${e.linkType} ${e.linkId} changed by rule ${e.ruleId}`);
      break;
    case 'balancing-start':
      w(`${t10}: Balancing the network:\n`); // FMT64 trailing \n
      break;
    case 'trial':
      w(`            Trial ${String(e.trial).padStart(2)}: ` +
        `relative flow change = ${fix(e.relativeFlowChange, 6)}`);
      break;
    case 'convergence-detail':
      if (e.measure === 'max-flow-change') {
        w('                      maximum  flow change = ' +
          `${fix(e.value, 4)} for ${e.elementType === 'link' ? 'Link' : 'Node'} ` +
          e.elementId);
      } else {
        // FMT68 carries a trailing \n
        w('                      maximum  head error  = ' +
          `${fix(e.value, 4)} for Link ${e.elementId}\n`);
      }
      break;
    case 'link-switch':
      w(`            ${e.linkType} ${e.linkId} switched from ${e.from} to ${e.to}`);
      break;
    case 'setting-change':
      w(`            ${e.linkType} ${e.linkId} setting changed to ` +
        fix(e.setting, 2));
      break;
    case 'valve-ill-conditioning':
      w(`${t10}: Valve ${e.valveId} caused ill-conditioning`);
      break;
    case 'balance':
      w(e.balanced
        ? `${t10}: Balanced after ${e.trials} trials`
        : `${t10}: Unbalanced after ${e.trials} trials ` +
          `(flow change = ${fix(e.relativeFlowChange ?? 0, 6)})`);
      break;
    case 'demand-reduction':
      w(e.nodeCount === 1
        ? '            1 node had its demand reduced by a total of ' +
          `${fix(e.totalReductionPercent, 2)}%`
        : `            ${e.nodeCount} nodes had demands reduced by a total of ` +
          `${fix(e.totalReductionPercent, 2)}%`);
      break;
    case 'tank-status':
      w(`${t10}: Tank ${e.nodeId} is ${e.state} at ` +
        `${fix(e.level, 2)} ${e.levelUnits}`);
      break;
    case 'reservoir-status':
      w(`${t10}: Reservoir ${e.nodeId} is ${e.state}`);
      break;
    case 'link-status':
      w(e.from === undefined
        ? `${t10}: ${e.linkType} ${e.linkId} ${e.to}` // FMT52, time zero
        : `${t10}: ${e.linkType} ${e.linkId} changed from ${e.from} to ${e.to}`);
      break;
    case 'ill-conditioned':
      w(`${t10}: System ill-conditioned at node ${e.nodeId}`);
      break;
    case 'warning':
      warningLine(w, e.warning, time);
      break;
  }
}

function warningLine(
  w: (s: string) => void,
  warning: SimulationWarning,
  time: Seconds,
): void {
  const t = clock(time);
  switch (warning.kind) {
    case 'system-unbalanced':
      w(`WARNING: System unbalanced at ${t} hrs.` +
        (warning.halted ? ' EXECUTION HALTED.' : ''));
      break;
    case 'max-trials-exceeded':
      w(`WARNING: Maximum trials exceeded at ${t} hrs. System may be unstable.`);
      break;
    case 'node-disconnected':
      w(`WARNING: Node ${warning.nodeId} disconnected at ${t} hrs`);
      break;
    case 'additional-nodes-disconnected':
      w(`WARNING: ${warning.count} additional nodes disconnected at ${t} hrs`);
      break;
    case 'system-disconnected-by-link':
      w(`WARNING: System disconnected because of Link ${warning.linkId}`);
      break;
    case 'pump-abnormal':
      w(`WARNING: Pump ${warning.pumpId} ${warning.state} at ${t} hrs.`);
      break;
    case 'valve-abnormal':
      w(`WARNING: ${warning.valveType} ${warning.valveId} ${warning.state} ` +
        `at ${t} hrs.`);
      break;
    case 'negative-pressures':
      w(`WARNING: Negative pressures at ${t} hrs.`);
      break;
  }
}
