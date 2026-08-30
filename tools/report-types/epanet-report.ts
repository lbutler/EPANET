/**
 * epanet-report.ts
 *
 * A complete TypeScript data model for the contents of an EPANET .rpt
 * report at its fullest setting (STATUS FULL, SUMMARY YES, NODES ALL,
 * LINKS ALL, ENERGY YES), including every error and warning path.
 *
 * Derived from an exhaustive inventory of every code path that writes to
 * the report file in OWA EPANET 2.3-dev (all 133 `writeline()` call sites
 * across src/, cross-checked against every format macro in src/text.h) —
 * see tools/report-replication/MISSING_API.md, appendix.  Each type below
 * cites the format string(s) it captures (FMTnn / WARNnn refer to macros
 * in src/text.h).
 *
 * Intended use: a streaming parser consumes report text line by line,
 * emits `ReportStreamEvent`s, and a reducer folds them into an
 * `EpanetReport`.  The model stores *data*, not layout: pagination
 * (`Page N` headers, form feeds) and the two-space line prefix are
 * parser concerns, not part of the model.
 *
 * Conventions:
 *  - All simulation times are seconds (parsed from the report's
 *    `h:mm:ss` clock format, whose hour field is unpadded and can
 *    exceed 24).
 *  - All numeric values are stored exactly as printed (already in the
 *    report's user units); unit strings ride alongside as metadata.
 *  - Wall-clock stamps (`Analysis begun ...`) are kept as raw strings.
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
 * `CV` is a check-valve pipe; it appears in status lines but never as a
 * results-table row suffix (only types above Pipe get a suffix).
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
 * use all eight; FMT57 switch lines and the results-table State column
 * collapse to `closed` / `open` / `active` before printing
 * (writestatchange / writelinktable in src/report.c).
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

/** The collapsed three-state view several lines print. */
export type CollapsedLinkState = 'closed' | 'open' | 'active';

/** Tank/reservoir states from FMT50/51 status lines. */
export type TankStateText = 'closed' | 'filling' | 'emptying' | 'overflowing';

/** Flow unit labels as printed in table headers (RptFlowUnitsTxt). */
export type FlowUnitsText =
  | 'cfs' | 'gpm' | 'mgd' | 'Imgd' | 'a-f/d'
  | 'L/s' | 'Lpm' | 'ML/d' | 'm3/h' | 'm3/d' | 'm3/s';

/** Pressure unit labels as printed (PressUnitsTxt — upper case in 2.3). */
export type PressureUnitsText = 'PSI' | 'KPA' | 'METERS' | 'BAR' | 'FEET';

/* ================================================================== */
/*  Report header                                                     */
/* ================================================================== */

/** The logo banner and page-1 datestamp (FMT18, LOGO1–6). */
export interface ReportHeader {
  /** Engine version from the banner, e.g. { major: 2, minor: 3, patch: 6 }. */
  version: { major: number; minor: number; patch: number };
  /** Raw ctime() string from the page-1 header, e.g. "Sun Aug 30 18:08:50 2026". */
  dateStamp: string;
  /**
   * The project's first [TITLE] line, as carried by `Page 2+` headers
   * (FMT82).  With SUMMARY off and PAGE > 0, those headers are the ONLY
   * place the title appears — so a parser that skips pagination should
   * still capture the title from the first continuation header it drops.
   */
  title?: string;
}

/* ================================================================== */
/*  Input summary (SUMMARY YES)                                       */
/* ================================================================== */

/** One `with <Param> below/above <value> <units>` line (FMT47/48). */
export interface ReportingLimit {
  parameter: string;
  kind: 'below' | 'above';
  value: number;
  units: string;
}

/** The `Quality Analysis` summary line (FMT29–32). */
export type QualityAnalysis =
  | { type: 'none' }
  | { type: 'chemical'; chemName: string }
  | { type: 'trace'; traceNode: NodeId }
  | { type: 'age' };

/**
 * The input summary block (writesummary, FMT19–FMT48).
 * Times print in `min` when the hydraulic step is under half an hour,
 * otherwise `hrs` — the same unit applies to timestep and duration.
 */
export interface InputSummary {
  /** Up to three non-empty [TITLE] lines, each truncated to 70 chars. */
  titleLines: string[];
  inputFile: string;
  counts: {
    junctions: number;
    reservoirs: number;
    tanks: number;
    pipes: number;
    pumps: number;
    valves: number;
  };
  headlossFormula: 'Hazen-Williams' | 'Darcy-Weisbach' | 'Chezy-Manning';
  demandModel: 'DDA' | 'PDA';
  timeUnits: 'hrs' | 'min';
  hydraulicTimestep: number;
  hydraulicAccuracy: number;
  /** FMT27d — only printed when the option is set. */
  headlossErrorLimit?: { value: number; units: string };
  /** FMT27e — only printed when the option is set. */
  flowChangeLimit?: { value: number; units: string };
  statusCheckFrequency: number;
  maximumTrialsChecked: number;
  dampingLimitThreshold: number;
  maximumTrials: number;
  /** Prints "None" whenever quality is off OR the duration is zero. */
  qualityAnalysis: QualityAnalysis;
  /** FMT33 — only when quality is on and duration > 0.  Always minutes. */
  qualityTimestepMinutes?: number;
  /**
   * FMT34 — only alongside qualityTimestepMinutes.  Units are the chem
   * units for chemical runs, `hrs` for age, `% from` for trace.
   */
  qualityTolerance?: { value: number; units: string };
  specificGravity: number;
  relativeKinematicViscosity: number;
  relativeChemicalDiffusivity: number;
  demandMultiplier: number;
  totalDuration: number;
  /** FMT40–48 `Reporting Criteria:` block. */
  reportingCriteria: {
    nodes: 'none' | 'all' | 'selected';
    nodeLimits: ReportingLimit[];
    links: 'none' | 'all' | 'selected';
    linkLimits: ReportingLimit[];
  };
}

/* ================================================================== */
/*  Hydraulic status log (STATUS YES / FULL)                          */
/* ================================================================== */

/**
 * One event in the hydraulic status stream.  Events are stored in report
 * order inside their time step — order is meaningful (control actions
 * precede the balance line; FULL trial traces interleave with switches).
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
   * Emitted from inside EN_nextH; its timestamp is the NEXT period's
   * time, so it appears between one step's block and the next.
   */
  | { kind: 'rule-action'; linkType: LinkTypeText; linkId: LinkId; ruleId: RuleId }
  /** FMT64 — `<t>: Balancing the network:` (STATUS FULL only). */
  | { kind: 'balancing-start' }
  /** FMT65 — `Trial  N: relative flow change = x` (STATUS FULL only). */
  | { kind: 'trial'; trial: number; relativeFlowChange: number }
  /**
   * FMT66/67/68 — `maximum flow change = x for Link|Node <id>` and
   * `maximum head error = x for Link <id>` (STATUS FULL only; can
   * repeat within one step as convergence is re-tested).  Head error is
   * only ever attributed to a link (reporthydbal, src/hydsolver.c).
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
   * FMT57 — `<LinkType> <id> switched from <state> to <state>`
   * (STATUS FULL only): a state switch BETWEEN solver trials.  Frequent
   * switches on the same links are the signature of an unstable model.
   * The engine collapses both states to closed/open/active before
   * printing (writestatchange, src/report.c).
   */
  | {
      kind: 'link-switch';
      linkType: LinkTypeText;
      linkId: LinkId;
      from: CollapsedLinkState;
      to: CollapsedLinkState;
    }
  /** FMT56 — `<LinkType> <id> setting changed to <x>` (STATUS FULL only). */
  | { kind: 'setting-change'; linkType: LinkTypeText; linkId: LinkId; setting: number }
  /** FMT61 — `<t>: Valve <id> caused ill-conditioning` (STATUS FULL only). */
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
   * FMT53 (`<t>: <LinkType> <id> changed from <state> to <state>`).
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
 * The `Hydraulic Status:` header (FMT49 + 71 dashes) precedes the first
 * step whenever the status level is YES or FULL.
 */
export interface HydraulicTimeStep {
  time: Seconds;
  events: StatusEvent[];
}

/* ================================================================== */
/*  End-of-run balance blocks (STATUS YES / FULL)                     */
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
 * `Water Quality Mass Balance<units>` block (writemassbalance).
 * `unitsSuffix` mirrors the header: trace runs print ` (mg)`; chemical
 * runs print ` (mg)` / ` (ug)` per their chem units or nothing for other
 * units; age prints ` (hrs)`.  Mass values print in `%12.5e` notation.
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
/*  Energy usage table (ENERGY YES)                                   */
/* ================================================================== */

/** One pump row of the `Energy Usage:` table (writeenergy, FMT71–75). */
export interface PumpEnergyRow {
  pumpId: LinkId;
  /** `Usage Factor` — percent of the run the pump was online. */
  usageFactorPercent: number;
  /** `Avg. Effic.` — percent. */
  averageEfficiencyPercent: number;
  /** `Kw-hr /Mgal` (US) or `Kw-hr /m3` (SI). */
  kwhPerFlow: number;
  averageKw: number;
  peakKw: number;
  costPerDay: number;
}

export interface EnergyUsage {
  /** Which per-volume header the table used. */
  perFlowUnits: '/Mgal' | '/m3';
  pumps: PumpEnergyRow[];
  /**
   * As printed.  Beware: the engine applies the demand-charge rate twice
   * (a known bug — savenergy() scales peak kW by the rate, writeenergy()
   * multiplies by it again), so with a non-zero rate this value is
   * rate x rate x peak-kW, not the correct charge.
   */
  demandCharge: number;
  totalCost: number;
}

/* ================================================================== */
/*  Results tables (NODES / LINKS)                                    */
/* ================================================================== */

/**
 * Which statistic the tables carry (the [TIMES] STATISTIC option).
 * `series` yields one table per reporting period titled
 * `Node Results at <t> hrs:` (or `Node Results:` for single-period
 * runs); the others yield a single table titled with the prefix —
 * `AVERAGE` / `MINIMUM` / `MAXIMUM` / `DIFFERENTIAL` (for range).
 */
export type ResultsStatistic =
  | 'series'
  | 'average'
  | 'minimum'
  | 'maximum'
  | 'range';

/** Node-table column identities, in the engine's fixed order. */
export type NodeFieldName =
  | 'elevation'
  | 'demand'
  | 'head'
  | 'pressure'
  | 'quality';

/** Link-table column identities, in the engine's fixed order. */
export type LinkFieldName =
  | 'length'
  | 'diameter'
  | 'flow'
  | 'velocity'
  | 'headloss'   // per 1000 length units for pipes; head units otherwise
  | 'quality'
  | 'status'     // textual: closed | open | active
  | 'setting'
  | 'reaction'
  | 'frictionFactor';

/**
 * A table column as printed: which field, its heading text and its units
 * text.  For the node quality column the heading is the chemical's name
 * (e.g. `Chlorine`, `% from`) and the units line carries the chem units
 * — or, for trace runs, the trace node's ID.
 */
export interface ResultsColumn<F extends string> {
  field: F;
  /** Column heading exactly as printed. */
  label: string;
  /** Units row text exactly as printed (may be empty). */
  units: string;
}

/**
 * One node row.  Only columns present in the table are populated.
 * Values are numbers as printed (fields switch to `%10.2e` scientific
 * notation above 1e6 in magnitude).
 */
export interface NodeResultRow {
  nodeId: NodeId;
  /** Trailing suffix for non-junctions (`Reservoir` / `Tank`). */
  nodeType?: Extract<NodeTypeText, 'Reservoir' | 'Tank'>;
  elevation?: number;
  demand?: number;
  head?: number;
  pressure?: number;
  quality?: number;
}

/** One link row.  `status` is the only textual column. */
export interface LinkResultRow {
  linkId: LinkId;
  /** Trailing suffix for link types above Pipe (`Pump`, `PRV`, ...). */
  linkType?: Exclude<LinkTypeText, 'CV' | 'Pipe'>;
  length?: number;
  diameter?: number;
  flow?: number;
  velocity?: number;
  headloss?: number;
  quality?: number;
  status?: 'closed' | 'open' | 'active';
  setting?: number;
  reaction?: number;
  frictionFactor?: number;
}

export interface NodeResultsTable {
  /** Reporting period time; null for single-period or statistic tables. */
  time: Seconds | null;
  columns: ResultsColumn<NodeFieldName>[];
  rows: NodeResultRow[];
}

export interface LinkResultsTable {
  time: Seconds | null;
  columns: ResultsColumn<LinkFieldName>[];
  rows: LinkResultRow[];
}

/* ================================================================== */
/*  Diagnostics: validation, parse and terminal errors                */
/* ================================================================== */

/**
 * Every `Error NNN: ...` (and related) line the engine can write into
 * the report, structured.  These appear in three places: before
 * `Analysis begun` (validation / open problems), interleaved mid-run
 * (a failing step's terminal error), or as input-parse diagnostics on a
 * file that failed to load.
 */
export type ReportDiagnostic =
  /**
   * validateproject() lines — `Error 225: ... node <id>`,
   * `Error 226/227: ... <pumpId>`, `Error 230/231: ... <curveId>`,
   * `Error 232: ... <patternId>`, and unlinked() `Error 234: ... <id>`.
   */
  | {
      kind: 'validation';
      code: number;
      message: string;
      elementKind: 'node' | 'pump' | 'pattern' | 'curve';
      elementId: string;
    }
  /**
   * Input parse pair (src/input2.c): `Error NNN: <msg> <token> in
   * [SECTION] section:` followed by an echo of the offending line.
   * The `section contents ignored` variant (299) has no section, and
   * the line-too-long variant (214) has no `Error NNN:` prefix at all —
   * it prints `<msg> section: <SECTION>` and the parser must synthesize
   * code 214 from the message text.
   */
  | {
      kind: 'parse';
      code: number;
      message: string;
      section?: string;
      offendingLine: string;
    }
  /**
   * Rule parse pair (ruleerrmsg in src/rules.c):
   * `Input Error NNN: <msg> Rule <label>:` (or the rules section when no
   * rule has been named yet) followed by the offending clause.  A rule
   * error is then re-reported by the generic parse handler, so both
   * diagnostics appear for the same clause.
   */
  | {
      kind: 'rule-parse';
      code: number;
      message: string;
      ruleId?: RuleId;
      offendingClause: string;
    }
  /**
   * A swallowed open-time warning: `Error 305: cannot open hydraulics
   * file`.  The engine falls back to a scratch file and continues —
   * EN_open still reports success.
   */
  | { kind: 'open-warning'; code: number; message: string }
  /**
   * A bare terminal `Error NNN: <msg>` line via errmsg(): e.g. 110
   * after failed validation, 200 after parse errors, 233 after
   * unlinked nodes, or a mid-run failure (308 etc.).  Error 309 is
   * deliberately never written (it IS the report-file write failure).
   */
  | { kind: 'terminal'; code: number; message: string };

/* ================================================================== */
/*  The whole report                                                  */
/* ================================================================== */

export interface EpanetReport {
  header: ReportHeader;

  /** Present only when SUMMARY YES (the default; most test files disable it). */
  summary?: InputSummary;

  /**
   * Diagnostics in report order.  On a clean run this is empty; on a
   * parse failure it is the only content after the header.
   */
  diagnostics: ReportDiagnostic[];

  /** Raw ctime() string from `Analysis begun ...`; null if never reached. */
  analysisBegun: string | null;

  /**
   * The hydraulic status stream.  Status lines proper need STATUS
   * YES/FULL, but WARNING events (and the status dump written when a
   * solve fails ill-conditioned) are gated by MESSAGES — on by default —
   * so this log can be non-empty even at STATUS NO.  Steps are in
   * simulation order; rule actions stamped with a step's time appear in
   * that step even though the engine emitted them a moment earlier.
   */
  statusLog: HydraulicTimeStep[];

  /** End-of-run flow balance (STATUS YES/FULL only). */
  flowBalance?: FlowBalance;

  /** End-of-run mass balance (STATUS YES/FULL with quality on). */
  massBalance?: MassBalance;

  /** Raw ctime() string from `Analysis ended ...`; null if the run failed. */
  analysisEnded: string | null;

  /** `Energy Usage:` table (ENERGY YES and at least one pump). */
  energy?: EnergyUsage;

  /** Which statistic the results tables carry. */
  resultsStatistic: ResultsStatistic;

  /** Node results tables, one per period (or a single statistic table). */
  nodeResults: NodeResultsTable[];

  /** Link results tables, one per period (or a single statistic table). */
  linkResults: LinkResultsTable[];

  /**
   * True when the report ran to completion (an `Analysis ended` stamp
   * was seen).  False for validation failures, parse failures, and
   * mid-run errors — in those cases `diagnostics` holds the reason.
   */
  complete: boolean;
}

/* ================================================================== */
/*  Streaming                                                         */
/* ================================================================== */

/**
 * The event stream a line-by-line parser emits; a reducer folds these
 * into an `EpanetReport`.  Events appear in report order.  Table rows
 * stream individually so a UI can render results incrementally.
 */
export type ReportStreamEvent =
  | { kind: 'header'; header: ReportHeader }
  | { kind: 'summary'; summary: InputSummary }
  | { kind: 'diagnostic'; diagnostic: ReportDiagnostic }
  | { kind: 'analysis-begun'; dateStamp: string }
  /** Emitted once when the `Hydraulic Status:` header is seen. */
  | { kind: 'status-start' }
  /**
   * One status event with its report timestamp.  `time` is null for the
   * lines printed without one — WARN03c, the FULL intra-step switch and
   * setting lines (FMT56/57), and the FULL trial / convergence-detail
   * continuation lines (FMT65–68) — attribute them to the current step.
   */
  | { kind: 'status-event'; time: Seconds | null; event: StatusEvent }
  | { kind: 'flow-balance'; balance: FlowBalance }
  | { kind: 'mass-balance'; balance: MassBalance }
  | { kind: 'analysis-ended'; dateStamp: string }
  | { kind: 'energy'; energy: EnergyUsage }
  | {
      kind: 'node-table-start';
      time: Seconds | null;
      statistic: ResultsStatistic;
      columns: ResultsColumn<NodeFieldName>[];
    }
  | { kind: 'node-row'; row: NodeResultRow }
  | {
      kind: 'link-table-start';
      time: Seconds | null;
      statistic: ResultsStatistic;
      columns: ResultsColumn<LinkFieldName>[];
    }
  | { kind: 'link-row'; row: LinkResultRow }
  | { kind: 'table-end' }
  | { kind: 'end'; complete: boolean };
