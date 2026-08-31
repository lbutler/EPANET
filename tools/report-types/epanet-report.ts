/**
 * epanet-report.ts
 *
 * TypeScript data model for the contents of an EPANET .rpt report as
 * epanet-js produces it.  epanet-js writes the input file, so the report
 * settings are pinned to one profile:
 *
 *     [REPORT]  Status FULL / Summary NO / Page 0 / Messages YES /
 *               Nodes NONE / Links NONE / Energy NO
 *     [TIMES]   Statistic NONE
 *
 * Under that profile a report is exactly:
 *
 *     Page-1 line + logo banner            -> ReportHeader
 *     [validation / parse diagnostics]     -> ReportDiagnostic[]
 *     Analysis begun <ctime>               -> analysisBegun
 *     Hydraulic Status: stream             -> HydraulicTimeStep[]
 *     Hydraulic Flow Balance               -> FlowBalance
 *     [Water Quality Mass Balance]         -> MassBalance   (quality on)
 *     Analysis ended <ctime>               -> analysisEnded
 *     [terminal error lines]               -> ReportDiagnostic[]
 *
 * There is no input summary, no pagination, no energy table and no
 * node/link results tables — results reach epanet-js through the API,
 * not the report.  The report's job is the status stream: what happened
 * inside the hydraulic loop (control/rule actions, link switching,
 * per-trial convergence, warnings) plus the closing balances.
 *
 * Derived from an exhaustive inventory of every code path that writes to
 * the report file in OWA EPANET 2.3-dev (all 133 `writeline()` call
 * sites, cross-checked against every format macro in src/text.h) — see
 * tools/report-replication/MISSING_API.md, appendix.  Each type cites
 * the format macro(s) it stores (FMTnn / WARNnn in src/text.h).
 *
 * Intended use: `ReportParser` (parse.ts) consumes report text and emits
 * `ReportStreamEvent`s; `ReportReducer` folds them into an
 * `EpanetReport`; `regenerateReport` (regenerate.ts) renders the model
 * back to the classic byte-exact text as a lossless-capture proof.
 *
 * Conventions:
 *  - Simulation times are seconds (parsed from `h:mm:ss` clocktime whose
 *    hour is unpadded and can exceed 24).
 *  - Numeric values are stored exactly as printed (already in the
 *    report's user units); unit strings ride alongside as metadata.
 *  - Wall-clock stamps (`Analysis begun ...`) are raw ctime() strings
 *    without their trailing newline.
 */

/* ================================================================== */
/*  Shared vocabulary                                                 */
/* ================================================================== */

/** Node ID as printed in the report (up to 31 chars). */
export type NodeId = string;
/** Link ID as printed in the report. */
export type LinkId = string;
/** Rule label as printed by rule action lines. */
export type RuleId = string;
/** Simulation time in seconds. */
export type Seconds = number;

/**
 * Link type words as the report prints them (LinkTxt in src/enumstxt.h).
 * `CV` is a check-valve pipe.
 */
export type LinkTypeText =
  | 'CV'
  | 'Pipe'
  | 'Pump'
  | 'PRV'
  | 'PSV'
  | 'PBV'
  | 'FCV'
  | 'TCV'
  | 'GPV'
  | 'PCV';

/** Node type words as printed (NodeTxt in src/enumstxt.h). */
export type NodeTypeText = 'Junction' | 'Reservoir' | 'Tank';

/**
 * The full internal link-state vocabulary as the status report prints it
 * (StatTxt in src/enumstxt.h).  Only the FMT52/53 status-change lines can
 * use all eight; the FMT57 intra-step switch lines collapse to
 * `closed` / `open` / `active` before printing (writestatchange).
 */
export type LinkStateText =
  | 'closed because cannot deliver head' // XHEAD
  | 'temporarily closed'                 // TEMPCLOSED
  | 'closed'
  | 'open'
  | 'active'
  | 'open but exceeds maximum flow'      // XFLOW
  | 'open but cannot deliver flow'       // XFCV
  | 'open but cannot deliver pressure';  // XPRESSURE

/** The collapsed three-state view the FMT57 switch lines print. */
export type CollapsedLinkState = 'closed' | 'open' | 'active';

/** Tank/reservoir states from FMT50/51 status lines. */
export type TankStateText = 'closed' | 'filling' | 'emptying' | 'overflowing';

/** Flow unit labels as printed (RptFlowUnitsTxt in src/enumstxt.h). */
export type FlowUnitsText =
  | 'cfs' | 'gpm' | 'mgd' | 'Imgd' | 'a-f/d'
  | 'L/s' | 'Lpm' | 'ML/d' | 'm3/h' | 'm3/d' | 'm3/s';

/* ================================================================== */
/*  Report header                                                     */
/* ================================================================== */

/** The `Page 1` datestamp line and logo banner (FMT18, LOGO1–6). */
export interface ReportHeader {
  /** Engine version from the banner, e.g. { major: 2, minor: 3, patch: 6 }. */
  version: { major: number; minor: number; patch: number };
  /** Raw ctime() string, e.g. "Sun Aug 30 18:08:50 2026". */
  dateStamp: string;
}

/* ================================================================== */
/*  Hydraulic status stream (STATUS FULL)                             */
/* ================================================================== */

/**
 * One event in the hydraulic status stream.  Events are stored in report
 * order inside their time step — order is meaningful (control actions
 * precede the `Balancing` header; trial traces interleave with the
 * intra-step switch lines they triggered).
 */
export type StatusEvent =
  /** FMT54 — `<t>: <LinkType> <id> changed by <NodeType> <id> control`. */
  | {
      kind: 'control-action';
      linkType: LinkTypeText;
      linkId: LinkId;
      cause: { kind: 'node'; nodeType: NodeTypeText; nodeId: NodeId };
    }
  /** FMT55 — `<t>: <LinkType> <id> changed by timer control`. */
  | {
      kind: 'control-action';
      linkType: LinkTypeText;
      linkId: LinkId;
      cause: { kind: 'timer' };
    }
  /**
   * FMT63 — `<t>: <LinkType> <id> changed by rule <label>`.
   * Emitted from inside EN_nextH and stamped with the NEXT period's
   * time, so it opens that step's block.
   */
  | { kind: 'rule-action'; linkType: LinkTypeText; linkId: LinkId; ruleId: RuleId }
  /** FMT64 — `<t>: Balancing the network:`. */
  | { kind: 'balancing-start' }
  /** FMT65 — `Trial  N: relative flow change = x`. */
  | { kind: 'trial'; trial: number; relativeFlowChange: number }
  /**
   * FMT66/67/68 — `maximum  flow change = x for Link|Node <id>` and
   * `maximum  head error  = x for Link <id>`; can repeat within one
   * step as convergence is re-tested after status changes.  Head error
   * is only ever attributed to a link (reporthydbal, src/hydsolver.c).
   */
  | {
      kind: 'convergence-detail';
      measure: 'max-flow-change';
      elementType: 'link' | 'node';
      elementId: string;
      value: number;
    }
  | {
      kind: 'convergence-detail';
      measure: 'max-head-error';
      elementType: 'link';
      elementId: LinkId;
      value: number;
    }
  /**
   * FMT57 — `<LinkType> <id> switched from <state> to <state>`: a state
   * switch BETWEEN solver trials.  Frequent switches on the same links
   * are the signature of an unstable model.
   */
  | {
      kind: 'link-switch';
      linkType: LinkTypeText;
      linkId: LinkId;
      from: CollapsedLinkState;
      to: CollapsedLinkState;
    }
  /** FMT56 — `<LinkType> <id> setting changed to <x>` (between trials). */
  | { kind: 'setting-change'; linkType: LinkTypeText; linkId: LinkId; setting: number }
  /** FMT61 — `<t>: Valve <id> caused ill-conditioning`. */
  | { kind: 'valve-ill-conditioning'; valveId: LinkId }
  /** FMT58/59 — `<t>: Balanced|Unbalanced after N trials (...)`. */
  | {
      kind: 'balance';
      balanced: boolean;
      trials: number;
      /** Present only on the Unbalanced variant. */
      relativeFlowChange?: number;
    }
  /** FMT69a/b — PDA `N node(s) had demand(s) reduced by a total of x%`. */
  | { kind: 'demand-reduction'; nodeCount: number; totalReductionPercent: number }
  /** FMT50 — `<t>: Tank <id> is <state> at <level> <units>`. */
  | {
      kind: 'tank-status';
      nodeId: NodeId;
      state: TankStateText;
      level: number;
      levelUnits: string;
    }
  /**
   * FMT51 — `<t>: Reservoir <id> is <state>` (no level).  Overflowing
   * requires a finite tank area, so a reservoir never prints it.
   */
  | {
      kind: 'reservoir-status';
      nodeId: NodeId;
      state: Exclude<TankStateText, 'overflowing'>;
    }
  /**
   * FMT52 (time zero: `<t>: <LinkType> <id> <state>`, no `from`) and
   * FMT53 (`<t>: <LinkType> <id> changed from <state> to <state>`):
   * the settled per-step status change, after the solver converged.
   */
  | {
      kind: 'link-status';
      linkType: LinkTypeText;
      linkId: LinkId;
      from?: LinkStateText;
      to: LinkStateText;
    }
  | { kind: 'warning'; warning: SimulationWarning }
  /** FMT62 — `<t>: System ill-conditioned at node <id>` (solve failed). */
  | { kind: 'ill-conditioned'; nodeId: NodeId };

/** WARN01–WARN06 lines (writehydwarn / disconnected in src/report.c). */
export type SimulationWarning =
  /** WARN01 — `System unbalanced at <t> hrs.` (+ ` EXECUTION HALTED.`). */
  | { kind: 'system-unbalanced'; halted: boolean }
  /** WARN02 — `Maximum trials exceeded ... System may be unstable.` */
  | { kind: 'max-trials-exceeded' }
  /** WARN03a — `Node <id> disconnected at <t> hrs` (up to 10 per step). */
  | { kind: 'node-disconnected'; nodeId: NodeId }
  /** WARN03b — `N additional nodes disconnected at <t> hrs`. */
  | { kind: 'additional-nodes-disconnected'; count: number }
  /** WARN03c — `System disconnected because of Link <id>` (no time). */
  | { kind: 'system-disconnected-by-link'; linkId: LinkId }
  /** WARN04 — `Pump <id> <state> at <t> hrs.` (XHEAD or XFLOW text). */
  | { kind: 'pump-abnormal'; pumpId: LinkId; state: LinkStateText }
  /** WARN05 — `<ValveType> <id> <state> at <t> hrs.` (XFCV/XPRESSURE). */
  | {
      kind: 'valve-abnormal';
      valveType: LinkTypeText;
      valveId: LinkId;
      state: LinkStateText;
    }
  /** WARN06 — `Negative pressures at <t> hrs.` */
  | { kind: 'negative-pressures' };

/**
 * All status events sharing one report timestamp, in report order.
 * Warnings always trail the step's other events (writehydstat runs
 * before writehydwarn).
 */
export interface HydraulicTimeStep {
  time: Seconds;
  events: StatusEvent[];
}

/* ================================================================== */
/*  End-of-run balance blocks                                         */
/* ================================================================== */

/** `Hydraulic Flow Balance (<units>)` block (writeflowbalance). */
export interface FlowBalance {
  units: FlowUnitsText;
  totalInflow: number;
  consumerDemand: number;
  demandDeficit: number;
  emitterFlow: number;
  leakageFlow: number;
  totalOutflow: number;
  storageFlow: number;
  flowRatio: number;
}

/**
 * `Water Quality Mass Balance<units>` block (writemassbalance), present
 * on quality runs.  `unitsSuffix` mirrors the header: chemical runs
 * print ` (mg)` / ` (ug)` per their chem units (nothing for other
 * units); trace prints ` (mg)`; age prints ` (hrs)`.  Mass values print
 * in `%12.5e` notation.
 */
export interface MassBalance {
  unitsSuffix: 'mg' | 'ug' | 'hrs' | null;
  initialMass: number;
  massInflow: number;
  massOutflow: number;
  massReacted: number;
  finalMass: number;
  massRatio: number;
  totalSegments: number;
}

/* ================================================================== */
/*  Diagnostics: validation, parse and terminal errors                */
/* ================================================================== */

/**
 * One error line the engine wrote into the report.  `text` is the whole
 * line exactly as printed (frame stripped), so nothing is lost; `kind`,
 * `code` and the optional fields are parsed conveniences.
 *
 *  - `validation`: a per-element line from validateproject()/unlinked()
 *    (codes 225/226/227/230/231/232/234); `elementId` is the offending
 *    element's ID (the line's last token).
 *  - `parse`: an input-parse line from src/input2.c, followed by an
 *    echo of the offending input line (`offendingLine`).  Covers the
 *    `... in [SECTION] section:` pairs, the `section contents ignored.`
 *    variant (299), and the line-too-long variant (214), which carries
 *    no `Error NNN:` prefix at all.
 *  - `rule-parse`: an `Input Error NNN: ... :` pair from ruleerrmsg()
 *    (src/rules.c); also followed by the offending clause.  A rule
 *    error is then re-reported by the generic parse handler, so both
 *    diagnostics appear for the same clause.
 *  - `open-warning`: a swallowed open-time problem — `Error 305: cannot
 *    open hydraulics file` — after which the engine falls back to a
 *    scratch file and the run continues.
 *  - `terminal`: a bare `Error NNN: <msg>` line via errmsg(), e.g. 110
 *    after failed validation or a halted run, 200 after parse errors,
 *    233 after unlinked nodes.
 */
export interface ReportDiagnostic {
  kind: 'validation' | 'parse' | 'rule-parse' | 'open-warning' | 'terminal';
  /** Numeric error code (214 is synthesized for the prefix-less shape). */
  code: number;
  /** The complete line as printed, e.g. `Error 225: invalid ... node 2`. */
  text: string;
  /** For `validation`: the offending element's ID. */
  elementId?: string;
  /** For `parse` / `rule-parse`: the echoed offending input line, verbatim. */
  offendingLine?: string;
  /**
   * True when the line appeared before `Analysis begun` (validation and
   * parse failures, Error 305); false for mid-run/terminal errors on a
   * run that started.
   */
  beforeAnalysis: boolean;
}

/* ================================================================== */
/*  The whole report                                                  */
/* ================================================================== */

export interface EpanetReport {
  header: ReportHeader;

  /**
   * Error lines in report order.  Empty on a clean run; on a parse or
   * validation failure they are the only content after the header.
   */
  diagnostics: ReportDiagnostic[];

  /** Raw ctime() string from `Analysis begun ...`; null if never reached. */
  analysisBegun: string | null;

  /**
   * The hydraulic status stream, one entry per reported timestamp, in
   * simulation order.  Rule actions stamped with a step's time open
   * that step even though the engine emitted them a moment earlier.
   */
  statusLog: HydraulicTimeStep[];

  /** End-of-run flow balance (written whenever the run finishes). */
  flowBalance?: FlowBalance;

  /** End-of-run mass balance (quality runs only). */
  massBalance?: MassBalance;

  /** Raw ctime() string from `Analysis ended ...`; null if the run failed. */
  analysisEnded: string | null;

  /**
   * True when the report ran to completion (an `Analysis ended` stamp
   * was seen).  False for validation failures, parse failures, and
   * mid-run errors — `diagnostics` then holds the reason.
   */
  complete: boolean;
}

/* ================================================================== */
/*  Streaming                                                        */
/* ================================================================== */

/**
 * The event stream `ReportParser` emits; `ReportReducer` folds these
 * into an `EpanetReport`.  Events appear in report order.
 */
export type ReportStreamEvent =
  | { kind: 'header'; header: ReportHeader }
  | { kind: 'diagnostic'; diagnostic: ReportDiagnostic }
  | { kind: 'analysis-begun'; dateStamp: string }
  /** Emitted once when the `Hydraulic Status:` header is seen. */
  | { kind: 'status-start' }
  /**
   * One status event with its report timestamp.  `time` is null for the
   * lines printed without one — WARN03c, the intra-step switch and
   * setting lines (FMT56/57), and the trial / convergence-detail
   * continuation lines (FMT65–68) — attribute them to the current step.
   */
  | { kind: 'status-event'; time: Seconds | null; event: StatusEvent }
  | { kind: 'flow-balance'; balance: FlowBalance }
  | { kind: 'mass-balance'; balance: MassBalance }
  | { kind: 'analysis-ended'; dateStamp: string }
  /**
   * A non-blank line the parser could not classify, verbatim (frame
   * stripped).  Never emitted for in-profile engine output; exists so a
   * production consumer degrades gracefully instead of throwing.
   */
  | { kind: 'unrecognized'; text: string }
  | { kind: 'end'; complete: boolean };
