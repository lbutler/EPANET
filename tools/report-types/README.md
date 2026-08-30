# EPANET report type model

`epanet-report.ts` is a complete TypeScript data model for the contents of
an EPANET `.rpt` report at its fullest setting — STATUS FULL, SUMMARY YES,
NODES/LINKS ALL, ENERGY YES — plus every warning and error path the engine
can write.

It exists so a parser (e.g. in epanet-js) can stream a report and fill
structured data instead of showing text: `ReportStreamEvent` is what a
line-by-line parser emits, `EpanetReport` is what a reducer accumulates.
The same structure could re-render the classic report, but the point is to
present it dynamically.

## Where it comes from

The model is derived from an exhaustive inventory of every code path that
writes to the report file in OWA EPANET 2.3-dev — all 133 `writeline()`
call sites, cross-checked against every format macro in `src/text.h` — and
validated against the engine's actual output on 59 corpus networks plus
purpose-built failure cases.  See `../report-replication/MISSING_API.md`
(appendix) for the inventory and `../report-replication/` for the harness
that produced it.  Each type cites the `FMTnn` / `WARNnn` macro it stores.

## Report anatomy → model

```
Page 1 header + logo banner            → ReportHeader
[input summary]                        → InputSummary          (SUMMARY YES)
[validation / parse diagnostics]       → ReportDiagnostic[]
Analysis begun <ctime>                 → analysisBegun
[Hydraulic Status: stream]             → HydraulicTimeStep[]   (STATUS YES/FULL)
[Hydraulic Flow Balance]               → FlowBalance           (STATUS YES/FULL)
[Water Quality Mass Balance]           → MassBalance           (STATUS YES/FULL + quality)
Analysis ended <ctime>                 → analysisEnded
[Energy Usage table]                   → EnergyUsage           (ENERGY YES)
[node/link tables per period]          → NodeResultsTable[] / LinkResultsTable[]
```

Note the order: the balance blocks print **before** `Analysis ended`, and
the energy + results tables print **after** it (they are written by
`EN_report`).

## Parsing notes (hard-won specifics)

- **Line frame.** Every line starts with a newline plus two spaces; blank
  lines come in two widths (`writeline("")` = 2 spaces,
  `writeline(" ")` = 3).  Strip the frame before matching.
- **Pagination is layout, not data.**  With `PAGE n` set, `\f` +
  `Page N    <title>` headers interrupt any section, and table headers
  reprint with a ` (continued)` suffix.  Skip them in the parser; the
  model ignores them.  (The status header's own `(continued)` variant is
  dead code and can never appear.)
- **Times.**  Simulation times are `h:mm:ss` with an unpadded hour that
  can exceed 24, right-aligned in 10 columns.  Wall-clock stamps are raw
  `ctime()` strings.
- **Numbers.**  Table cells switch to `%10.2e` scientific notation above
  1e6 in magnitude; mass balance values are always `%12.5e`.
- **Timeless status lines.**  WARN03c and the FULL intra-step
  `switched from` / `setting changed to` lines carry no timestamp —
  attribute them to the current step.
- **Rule action timestamps.**  FMT63 lines are emitted from inside
  `EN_nextH` and stamped with the *next* period's time, so they appear
  between one step's block and the next.
- **Statistic runs.**  With `STATISTIC AVERAGE/…` there is a single
  node + link table titled `AVERAGE Node Results:` (range prints
  `DIFFERENTIAL`); single-period runs title tables `Node Results:` with
  no time.  `resultsStatistic` + `time: null` cover both.
- **Quality column header.**  The node table's quality column is headed
  by the chemical's *name* over its *units* — for trace runs that is
  `% from` over the trace node's ID.
- **Diagnostics come in pairs.**  Input parse errors are a message line
  plus an echo of the offending input line; rule errors print their own
  `Input Error …` pair and are then re-reported by the generic handler.
- **Known engine bug, stored as printed.**  The energy table's Demand
  Charge has the rate applied twice (`savenergy()` then `writeenergy()`);
  the model stores the printed value and documents the bug.
- **Warnings repeat per step.**  The same warning can appear at every
  reporting period; each occurrence is its own `StatusEvent`.

## Checking

The file compiles clean under `tsc --strict --noEmit` (TypeScript 5.6).
