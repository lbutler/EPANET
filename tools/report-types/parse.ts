/**
 * parse.ts — streaming parser for EPANET .rpt reports (fixed profile:
 * STATUS FULL, SUMMARY NO, PAGE 0, NODES/LINKS NONE, ENERGY NO).
 *
 * `ReportParser` accepts text in arbitrary chunks, splits it into report
 * lines, and emits one `ReportStreamEvent` per meaningful line (balance
 * blocks are assembled and emitted whole).  Layout — the two-space line
 * frame, blank lines, dash/star/equals rules — is consumed here and
 * never reaches the events.  A line that matches nothing becomes an
 * `unrecognized` event rather than an error, so a production consumer
 * degrades gracefully.
 *
 * `ReportReducer` folds the event stream into an `EpanetReport`,
 * grouping status events into time steps (timeless lines — trials,
 * convergence details, intra-step switches, WARN03c — attach to the
 * current step).
 *
 * `parseReport(text)` is the one-shot convenience over both.
 */

import type {
  CollapsedLinkState,
  EpanetReport,
  FlowBalance,
  FlowUnitsText,
  HydraulicTimeStep,
  LinkStateText,
  LinkTypeText,
  MassBalance,
  NodeTypeText,
  ReportDiagnostic,
  ReportHeader,
  ReportStreamEvent,
  SimulationWarning,
  StatusEvent,
  TankStateText,
} from './epanet-report.js';

/* ------------------------------------------------------------------ */
/*  Lexical vocabulary                                                */
/* ------------------------------------------------------------------ */

const NUM = String.raw`-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?`;
const T = String.raw`(\d+):(\d\d):(\d\d)`;
const LT = '(CV|Pipe|Pump|PRV|PSV|PBV|FCV|TCV|GPV|PCV)';
// Longest alternatives first so anchored matches resolve without surprises.
const ST =
  '(closed because cannot deliver head|temporarily closed' +
  '|open but exceeds maximum flow|open but cannot deliver flow' +
  '|open but cannot deliver pressure|closed|open|active)';
const TANKST = '(closed|filling|emptying|overflowing)';

const toSeconds = (h: string, m: string, s: string): number =>
  Number(h) * 3600 + Number(m) * 60 + Number(s);

/** Validation codes written per-element by validateproject()/unlinked(). */
const VALIDATION_CODES = new Set([225, 226, 227, 230, 231, 232, 234]);

/* ------------------------------------------------------------------ */
/*  Line patterns (frame already stripped)                            */
/* ------------------------------------------------------------------ */

const rx = {
  page1: /^Page 1 {2,}(\S.*)$/,
  logoVersion: /^\* +Version (\d+)\.(\d+)\.(\d+) +\*$/,
  logoOther: /^\*.*\*$|^\*{5,}$/,
  dashRule: /^-{5,}$/,
  equalsRule: /^={5,}$/,

  begun: /^Analysis begun (.+)$/,
  ended: /^Analysis ended (.+)$/,
  statusHeader: /^Hydraulic Status:$/,

  // --- timed status lines: "<%10s time>: <rest>" ------------------
  timed: new RegExp(`^ *${T}: (.*)$`),
  balancingStart: /^Balancing the network:$/,
  controlNode: new RegExp(
    `^${LT} (\\S+) changed by (Junction|Reservoir|Tank) (\\S+) control$`),
  controlTimer: new RegExp(`^${LT} (\\S+) changed by timer control$`),
  ruleAction: new RegExp(`^${LT} (\\S+) changed by rule (.+)$`),
  balanced: /^Balanced after (\d+) trials$/,
  unbalanced: new RegExp(
    `^Unbalanced after (\\d+) trials \\(flow change = (${NUM})\\)$`),
  tankStatus: new RegExp(`^Tank (\\S+) is ${TANKST} at (${NUM}) (\\S+)$`),
  reservoirStatus: new RegExp(`^Reservoir (\\S+) is ${TANKST}$`),
  valveIll: /^Valve (\S+) caused ill-conditioning$/,
  illConditioned: /^System ill-conditioned at node (.+)$/,
  linkChanged: new RegExp(`^${LT} (\\S+) changed from ${ST} to ${ST}$`),
  linkInitial: new RegExp(`^${LT} (\\S+) ${ST}$`),

  // --- timeless status lines (attach to the current step) ---------
  trial: new RegExp(`^ *Trial +(\\d+): relative flow change = (${NUM})$`),
  convFlow: new RegExp(
    `^ *maximum +flow change = (${NUM}) for (Link|Node) (.+)$`),
  convHead: new RegExp(`^ *maximum +head error += (${NUM}) for Link (.+)$`),
  linkSwitch: new RegExp(
    `^ *${LT} (\\S+) switched from (closed|open|active) to (closed|open|active)$`),
  settingChange: new RegExp(`^ *${LT} (\\S+) setting changed to (${NUM})$`),
  pdaOne: new RegExp(
    `^ *1 node had its demand reduced by a total of (${NUM})%$`),
  pdaMany: new RegExp(
    `^ *(\\d+) nodes had demands reduced by a total of (${NUM})%$`),

  // --- warnings ---------------------------------------------------
  warnUnbalanced: new RegExp(
    `^WARNING: System unbalanced at ${T} hrs\\.( EXECUTION HALTED\\.)?$`),
  warnUnstable: new RegExp(
    `^WARNING: Maximum trials exceeded at ${T} hrs\\. System may be unstable\\.$`),
  warnDisconnected: new RegExp(`^WARNING: Node (\\S+) disconnected at ${T} hrs$`),
  warnDisconnectedMore: new RegExp(
    `^WARNING: (\\d+) additional nodes disconnected at ${T} hrs$`),
  warnDisconnectedLink: /^WARNING: System disconnected because of Link (.+)$/,
  warnPump: new RegExp(`^WARNING: Pump (\\S+) ${ST} at ${T} hrs\\.$`),
  warnValve: new RegExp(`^WARNING: ${LT} (\\S+) ${ST} at ${T} hrs\\.$`),
  warnNegative: new RegExp(`^WARNING: Negative pressures at ${T} hrs\\.$`),

  // --- balance blocks ---------------------------------------------
  flowBalHeader: /^Hydraulic Flow Balance \((\S+)\)$/,
  flowBalTerm: new RegExp(
    '^(Total Inflow|Consumer Demand|Demand Deficit|Emitter Flow' +
    `|Leakage Flow|Total Outflow|Storage Flow|Flow Ratio): +(${NUM})$`),
  massBalHeader: /^Water Quality Mass Balance(?: \((mg|ug|hrs)\))?$/,
  massBalTerm: new RegExp(
    `^(Initial Mass|Mass Inflow|Mass Outflow|Mass Reacted|Final Mass|Mass Ratio): +(${NUM})$`),
  massBalSegments: /^Total Segments: +(\d+)$/,

  // --- diagnostics ------------------------------------------------
  inputError: /^Input Error (\d+): (.*)$/,
  error: /^Error (\d+): (.*)$/,
  // input2.c line-too-long shape (no "Error NNN:" prefix): "<msg> section: [SECT]"
  tooLong: /^\S.* section: \[?[A-Z]+\]?$/,
  // input2.c pair shapes whose next line is the echoed offending line
  parseEcho: / in \[?[A-Z]+\]? section:$|: section contents ignored\.$/,
};

/* ------------------------------------------------------------------ */
/*  Tokenizer                                                         */
/* ------------------------------------------------------------------ */

type FlowBalDraft = { units: FlowUnitsText; rules: number; terms: Map<string, number> };
type MassBalDraft = {
  suffix: 'mg' | 'ug' | 'hrs' | null;
  rules: number;
  terms: Map<string, number>;
  segments: number;
};

export class ReportParser {
  private readonly onEvent: (ev: ReportStreamEvent) => void;
  private buf = '';
  private ended = false;

  private dateStamp = '';
  private headerEmitted = false;
  private sawBegun = false;
  private sawEnded = false;

  /** Diagnostic waiting for its echoed offending line. */
  private pendingEcho: ReportDiagnostic | null = null;
  private flowDraft: FlowBalDraft | null = null;
  private massDraft: MassBalDraft | null = null;

  constructor(onEvent: (ev: ReportStreamEvent) => void) {
    this.onEvent = onEvent;
  }

  /** Feed a chunk of report text (any chunking, including mid-line). */
  write(chunk: string): void {
    if (this.ended) throw new Error('ReportParser: write() after end()');
    this.buf += chunk;
    let nl;
    while ((nl = this.buf.indexOf('\n')) >= 0) {
      this.line(this.buf.slice(0, nl));
      this.buf = this.buf.slice(nl + 1);
    }
  }

  /** Flush the final line and emit the `end` event. */
  end(): void {
    if (this.ended) return;
    this.ended = true;
    if (this.buf.length > 0) this.line(this.buf);
    this.buf = '';
    this.flushEcho();
    this.onEvent({ kind: 'end', complete: this.sawEnded });
  }

  /* ---------------------------------------------------------------- */

  private emit(ev: ReportStreamEvent): void {
    this.onEvent(ev);
  }

  private flushEcho(): void {
    if (this.pendingEcho) {
      this.emit({ kind: 'diagnostic', diagnostic: this.pendingEcho });
      this.pendingEcho = null;
    }
  }

  private statusEvent(time: number | null, event: StatusEvent): void {
    this.emit({ kind: 'status-event', time, event });
  }

  private line(raw: string): void {
    if (raw.endsWith('\r')) raw = raw.slice(0, -1);
    // Blank lines (0, 2 or 3 spaces wide) are layout.
    if (raw.trim() === '') return;
    // Every content line carries writeline()'s two-space frame.
    const s = raw.startsWith('  ') ? raw.slice(2) : raw;

    // An echoed offending input line directly follows its diagnostic.
    if (this.pendingEcho) {
      this.pendingEcho.offendingLine = s;
      this.flushEcho();
      return;
    }

    if (this.flowDraft && this.flowBalanceLine(s)) return;
    if (this.massDraft && this.massBalanceLine(s)) return;

    let m: RegExpExecArray | null;

    // ---- header ---------------------------------------------------
    if (!this.headerEmitted) {
      if ((m = rx.page1.exec(s))) {
        this.dateStamp = m[1];
        return;
      }
      if ((m = rx.logoVersion.exec(s))) {
        const header: ReportHeader = {
          version: { major: Number(m[1]), minor: Number(m[2]), patch: Number(m[3]) },
          dateStamp: this.dateStamp,
        };
        this.headerEmitted = true;
        this.emit({ kind: 'header', header });
        return;
      }
      if (rx.logoOther.test(s)) return;
    }

    // ---- section marks --------------------------------------------
    if (rx.dashRule.test(s) || rx.equalsRule.test(s) || /^\*{5,}$/.test(s)) {
      return;
    }
    if ((m = rx.begun.exec(s))) {
      this.sawBegun = true;
      this.emit({ kind: 'analysis-begun', dateStamp: m[1] });
      return;
    }
    if ((m = rx.ended.exec(s))) {
      this.sawEnded = true;
      this.emit({ kind: 'analysis-ended', dateStamp: m[1] });
      return;
    }
    if (rx.statusHeader.test(s)) {
      this.emit({ kind: 'status-start' });
      return;
    }
    if ((m = rx.flowBalHeader.exec(s))) {
      this.flowDraft = { units: m[1] as FlowUnitsText, rules: 0, terms: new Map() };
      return;
    }
    if ((m = rx.massBalHeader.exec(s))) {
      this.massDraft = {
        suffix: (m[1] as 'mg' | 'ug' | 'hrs' | undefined) ?? null,
        rules: 0,
        terms: new Map(),
        segments: 0,
      };
      return;
    }

    // ---- status stream --------------------------------------------
    if ((m = rx.timed.exec(s))) {
      if (this.timedStatusLine(toSeconds(m[1], m[2], m[3]), m[4])) return;
    }
    if (this.timelessStatusLine(s)) return;
    if (this.warningLine(s)) return;

    // ---- diagnostics ----------------------------------------------
    if (this.diagnosticLine(s)) return;

    this.emit({ kind: 'unrecognized', text: s });
  }

  /** Lines opening with the 10-column clocktime prefix. */
  private timedStatusLine(time: number, rest: string): boolean {
    let m: RegExpExecArray | null;
    if (rx.balancingStart.test(rest)) {
      this.statusEvent(time, { kind: 'balancing-start' });
    } else if ((m = rx.controlNode.exec(rest))) {
      this.statusEvent(time, {
        kind: 'control-action',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        cause: { kind: 'node', nodeType: m[3] as NodeTypeText, nodeId: m[4] },
      });
    } else if ((m = rx.controlTimer.exec(rest))) {
      this.statusEvent(time, {
        kind: 'control-action',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        cause: { kind: 'timer' },
      });
    } else if ((m = rx.ruleAction.exec(rest))) {
      this.statusEvent(time, {
        kind: 'rule-action',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        ruleId: m[3],
      });
    } else if ((m = rx.balanced.exec(rest))) {
      this.statusEvent(time, { kind: 'balance', balanced: true, trials: Number(m[1]) });
    } else if ((m = rx.unbalanced.exec(rest))) {
      this.statusEvent(time, {
        kind: 'balance',
        balanced: false,
        trials: Number(m[1]),
        relativeFlowChange: parseFloat(m[2]),
      });
    } else if ((m = rx.tankStatus.exec(rest))) {
      this.statusEvent(time, {
        kind: 'tank-status',
        nodeId: m[1],
        state: m[2] as TankStateText,
        level: parseFloat(m[3]),
        levelUnits: m[4],
      });
    } else if ((m = rx.reservoirStatus.exec(rest))) {
      this.statusEvent(time, {
        kind: 'reservoir-status',
        nodeId: m[1],
        state: m[2] as Exclude<TankStateText, 'overflowing'>,
      });
    } else if ((m = rx.valveIll.exec(rest))) {
      this.statusEvent(time, { kind: 'valve-ill-conditioning', valveId: m[1] });
    } else if ((m = rx.illConditioned.exec(rest))) {
      this.statusEvent(time, { kind: 'ill-conditioned', nodeId: m[1] });
    } else if ((m = rx.linkChanged.exec(rest))) {
      this.statusEvent(time, {
        kind: 'link-status',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        from: m[3] as LinkStateText,
        to: m[4] as LinkStateText,
      });
    } else if ((m = rx.linkInitial.exec(rest))) {
      this.statusEvent(time, {
        kind: 'link-status',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        to: m[3] as LinkStateText,
      });
    } else {
      return false;
    }
    return true;
  }

  /** Continuation lines printed without a timestamp. */
  private timelessStatusLine(s: string): boolean {
    let m: RegExpExecArray | null;
    if ((m = rx.trial.exec(s))) {
      this.statusEvent(null, {
        kind: 'trial',
        trial: Number(m[1]),
        relativeFlowChange: parseFloat(m[2]),
      });
    } else if ((m = rx.convFlow.exec(s))) {
      this.statusEvent(null, {
        kind: 'convergence-detail',
        measure: 'max-flow-change',
        elementType: m[2] === 'Link' ? 'link' : 'node',
        elementId: m[3],
        value: parseFloat(m[1]),
      });
    } else if ((m = rx.convHead.exec(s))) {
      this.statusEvent(null, {
        kind: 'convergence-detail',
        measure: 'max-head-error',
        elementType: 'link',
        elementId: m[2],
        value: parseFloat(m[1]),
      });
    } else if ((m = rx.linkSwitch.exec(s))) {
      this.statusEvent(null, {
        kind: 'link-switch',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        from: m[3] as CollapsedLinkState,
        to: m[4] as CollapsedLinkState,
      });
    } else if ((m = rx.settingChange.exec(s))) {
      this.statusEvent(null, {
        kind: 'setting-change',
        linkType: m[1] as LinkTypeText,
        linkId: m[2],
        setting: parseFloat(m[3]),
      });
    } else if ((m = rx.pdaOne.exec(s))) {
      this.statusEvent(null, {
        kind: 'demand-reduction',
        nodeCount: 1,
        totalReductionPercent: parseFloat(m[1]),
      });
    } else if ((m = rx.pdaMany.exec(s))) {
      this.statusEvent(null, {
        kind: 'demand-reduction',
        nodeCount: Number(m[1]),
        totalReductionPercent: parseFloat(m[2]),
      });
    } else {
      return false;
    }
    return true;
  }

  private warningLine(s: string): boolean {
    let m: RegExpExecArray | null;
    const emitWarn = (
      time: number | null,
      warning: SimulationWarning,
    ): void => this.statusEvent(time, { kind: 'warning', warning });

    if ((m = rx.warnUnbalanced.exec(s))) {
      emitWarn(toSeconds(m[1], m[2], m[3]),
        { kind: 'system-unbalanced', halted: m[4] !== undefined });
    } else if ((m = rx.warnUnstable.exec(s))) {
      emitWarn(toSeconds(m[1], m[2], m[3]), { kind: 'max-trials-exceeded' });
    } else if ((m = rx.warnDisconnected.exec(s))) {
      emitWarn(toSeconds(m[2], m[3], m[4]),
        { kind: 'node-disconnected', nodeId: m[1] });
    } else if ((m = rx.warnDisconnectedMore.exec(s))) {
      emitWarn(toSeconds(m[2], m[3], m[4]),
        { kind: 'additional-nodes-disconnected', count: Number(m[1]) });
    } else if ((m = rx.warnDisconnectedLink.exec(s))) {
      emitWarn(null, { kind: 'system-disconnected-by-link', linkId: m[1] });
    } else if ((m = rx.warnPump.exec(s))) {
      emitWarn(toSeconds(m[3], m[4], m[5]),
        { kind: 'pump-abnormal', pumpId: m[1], state: m[2] as LinkStateText });
    } else if ((m = rx.warnValve.exec(s))) {
      emitWarn(toSeconds(m[4], m[5], m[6]), {
        kind: 'valve-abnormal',
        valveType: m[1] as LinkTypeText,
        valveId: m[2],
        state: m[3] as LinkStateText,
      });
    } else if ((m = rx.warnNegative.exec(s))) {
      emitWarn(toSeconds(m[1], m[2], m[3]), { kind: 'negative-pressures' });
    } else {
      return false;
    }
    return true;
  }

  private flowBalanceLine(s: string): boolean {
    const draft = this.flowDraft!;
    let m: RegExpExecArray | null;
    if (rx.equalsRule.test(s)) {
      draft.rules++;
      if (draft.rules === 2) {
        const t = (name: string): number => draft.terms.get(name) ?? NaN;
        const balance: FlowBalance = {
          units: draft.units,
          totalInflow: t('Total Inflow'),
          consumerDemand: t('Consumer Demand'),
          demandDeficit: t('Demand Deficit'),
          emitterFlow: t('Emitter Flow'),
          leakageFlow: t('Leakage Flow'),
          totalOutflow: t('Total Outflow'),
          storageFlow: t('Storage Flow'),
          flowRatio: t('Flow Ratio'),
        };
        this.flowDraft = null;
        this.emit({ kind: 'flow-balance', balance });
      }
      return true;
    }
    if ((m = rx.flowBalTerm.exec(s))) {
      draft.terms.set(m[1], parseFloat(m[2]));
      return true;
    }
    return false;
  }

  private massBalanceLine(s: string): boolean {
    const draft = this.massDraft!;
    let m: RegExpExecArray | null;
    if (rx.equalsRule.test(s)) {
      draft.rules++;
      if (draft.rules === 2) {
        const t = (name: string): number => draft.terms.get(name) ?? NaN;
        const balance: MassBalance = {
          unitsSuffix: draft.suffix,
          initialMass: t('Initial Mass'),
          massInflow: t('Mass Inflow'),
          massOutflow: t('Mass Outflow'),
          massReacted: t('Mass Reacted'),
          finalMass: t('Final Mass'),
          massRatio: t('Mass Ratio'),
          totalSegments: draft.segments,
        };
        this.massDraft = null;
        this.emit({ kind: 'mass-balance', balance });
      }
      return true;
    }
    if ((m = rx.massBalTerm.exec(s))) {
      draft.terms.set(m[1], parseFloat(m[2]));
      return true;
    }
    if ((m = rx.massBalSegments.exec(s))) {
      draft.segments = Number(m[1]);
      return true;
    }
    return false;
  }

  private diagnosticLine(s: string): boolean {
    let m: RegExpExecArray | null;
    const before = !this.sawBegun;

    if ((m = rx.inputError.exec(s))) {
      // ruleerrmsg() pair — the offending clause follows.
      this.pendingEcho = {
        kind: 'rule-parse',
        code: Number(m[1]),
        text: s,
        beforeAnalysis: before,
      };
      return true;
    }
    if ((m = rx.error.exec(s))) {
      const code = Number(m[1]);
      if (rx.parseEcho.test(s)) {
        // inperrmsg() pair — the echoed input line follows.
        this.pendingEcho = { kind: 'parse', code, text: s, beforeAnalysis: before };
        return true;
      }
      if (code === 305) {
        this.emit({
          kind: 'diagnostic',
          diagnostic: { kind: 'open-warning', code, text: s, beforeAnalysis: before },
        });
        return true;
      }
      if (VALIDATION_CODES.has(code)) {
        const words = s.split(' ');
        this.emit({
          kind: 'diagnostic',
          diagnostic: {
            kind: 'validation',
            code,
            text: s,
            elementId: words[words.length - 1],
            beforeAnalysis: before,
          },
        });
        return true;
      }
      this.emit({
        kind: 'diagnostic',
        diagnostic: { kind: 'terminal', code, text: s, beforeAnalysis: before },
      });
      return true;
    }
    if (before && rx.tooLong.test(s)) {
      // input2.c's line-too-long message has no "Error NNN:" prefix.
      this.pendingEcho = { kind: 'parse', code: 214, text: s, beforeAnalysis: before };
      return true;
    }
    return false;
  }
}

/* ------------------------------------------------------------------ */
/*  Reducer                                                           */
/* ------------------------------------------------------------------ */

export class ReportReducer {
  readonly report: EpanetReport = {
    header: { version: { major: 0, minor: 0, patch: 0 }, dateStamp: '' },
    diagnostics: [],
    analysisBegun: null,
    statusLog: [],
    analysisEnded: null,
    complete: false,
  };

  private currentStep: HydraulicTimeStep | null = null;

  handle(ev: ReportStreamEvent): void {
    const r = this.report;
    switch (ev.kind) {
      case 'header':
        r.header = ev.header;
        break;
      case 'diagnostic':
        r.diagnostics.push(ev.diagnostic);
        break;
      case 'analysis-begun':
        r.analysisBegun = ev.dateStamp;
        break;
      case 'status-start':
        break;
      case 'status-event': {
        if (ev.time !== null &&
            (this.currentStep === null || this.currentStep.time !== ev.time)) {
          this.currentStep = { time: ev.time, events: [] };
          r.statusLog.push(this.currentStep);
        }
        if (this.currentStep === null) {
          this.currentStep = { time: 0, events: [] };
          r.statusLog.push(this.currentStep);
        }
        this.currentStep.events.push(ev.event);
        break;
      }
      case 'flow-balance':
        r.flowBalance = ev.balance;
        break;
      case 'mass-balance':
        r.massBalance = ev.balance;
        break;
      case 'analysis-ended':
        r.analysisEnded = ev.dateStamp;
        break;
      case 'unrecognized':
        break;
      case 'end':
        r.complete = ev.complete;
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/*  One-shot convenience                                              */
/* ------------------------------------------------------------------ */

/** Parse a complete report; also returns any unrecognized lines. */
export function parseReport(
  text: string,
): { report: EpanetReport; unrecognized: string[] } {
  const reducer = new ReportReducer();
  const unrecognized: string[] = [];
  const parser = new ReportParser((ev) => {
    if (ev.kind === 'unrecognized') unrecognized.push(ev.text);
    reducer.handle(ev);
  });
  parser.write(text);
  parser.end();
  return { report: reducer.report, unrecognized };
}
