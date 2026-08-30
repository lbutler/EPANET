# Toolkit API gaps found by replicating the EPANET report

Goal: reproduce the engine's native `.rpt` report 1:1 using **only** public
toolkit API calls (`include/epanet2_2.h`), so the same data could later be
rendered in any other format (HTML, JSON, ...).  `repgen` writes the native
report and a pure-API replica side by side; `compare-reports.sh` diffs them
(ignoring wall-clock timestamps) across the three bundled example networks
and the [epanet-example-networks](https://github.com/OpenWaterAnalytics/epanet-example-networks)
regression corpus (59 networks total).

**Status (first pass, `STATUS NO`):** 55 of 59 networks replicate
byte-for-byte, in both "as-is" mode and with `NODES ALL`, `LINKS ALL` and
`ENERGY YES` forced on; a third sweep over a variant corpus (every network's
`SUMMARY` forced on in the INP, plus a paginated `PAGE 55` variant) passes
56 of 60 the same way.  The 4 remaining networks differ only by a `WARNING`
line that gap #3 below makes impossible to reconstruct.

Legend: **GAP** = value cannot be obtained through the public API.
**WORKAROUND** = obtainable, but only by duplicating engine internals or
going around the API.

---

## 1. `[REPORT]` settings are write-only  (GAP, headline finding)

The report's *shape* is controlled by settings parsed from `[REPORT]` (or
set with `EN_setreport`), stored in the engine's `Report` struct.  The only
one that can be read back is the status level
(`EN_getoption(EN_STATUS_REPORT)`).  **None of the following have getters:**

| Setting                    | Set by                          | Needed for                          |
|----------------------------|---------------------------------|-------------------------------------|
| summary on/off             | `SUMMARY YES/NO`                | whether the summary block exists    |
| energy table on/off        | `ENERGY YES/NO`                 | whether the energy table exists     |
| messages on/off            | `MESSAGES YES/NO`               | whether warnings appear             |
| node selection (none/all/list + per-node flags) | `NODES ...`    | node tables + summary criteria line |
| link selection             | `LINKS ...`                     | link tables + summary criteria line |
| page size                  | `PAGE n`                        | pagination + page headers           |
| per-field enable/precision/limits | `PRESSURE PRECISION 3`, `DEMAND BELOW x`, ... | table columns, decimals, row filtering, criteria lines |
| secondary report file      | `FILE name`                     | -                                   |

*Workaround used:* re-parse the `[REPORT]` section of the INP file
(`inp_sniff.c`), duplicating `reportdata()` from `src/input3.c`.

*Suggested API:* getters mirroring the setters, e.g.
`EN_getreportoption(ph, EN_RPT_SUMMARY | EN_RPT_ENERGY | EN_RPT_PAGESIZE |
EN_RPT_NODESELECTION, ...)` plus per-field
`EN_getreportfield(ph, field, *enabled, *precision, *lowLimit, *highLimit)`.

## 2. Cumulative pump energy usage is not exposed  (WORKAROUND)

The Energy Usage table prints, per pump, end-of-run cumulative statistics:
usage factor (% time online), average efficiency, kWh/Mgal (or kWh/m3),
average kW, peak kW, cost/day - plus the system peak-kW demand charge.  The
engine accumulates all of these internally (`Spump.Energy`, `hyd->Emax` in
`addenergy()`, finalized in `savenergy()`), but the API only exposes
*instantaneous* values (`EN_ENERGY`, `EN_PUMP_EFFIC`).

*Workaround used:* re-integrate the statistics caller-side at every
hydraulic step (`accumulateEnergy()`/`finalizeEnergy()` in `collect.c`),
duplicating engine internals: the `QZERO` flow clamp, price-pattern lookup
`(Htime+Pstart)/Pstep`, the `dt=1 hr` rule for single-period runs, and the
kWh-per-flow unit conversions.  This only works when driving the simulation
step by step - the data is unobtainable after a plain `EN_solveH()` /
`ENepanet()` run.  Replicates to the cent on the whole corpus.

*Suggested API:* `EN_getpumpenergy(ph, linkIndex, code, *value)` with codes
for the six table columns, plus `EN_getenergydemandcharge` (or an
`EN_getstatistic` extension) for system peak kW.

## 3. Abnormal valve states are not exposed  (GAP - blocks 4 networks)

`writehydwarn()` emits `WARNING: FCV 47 open but cannot deliver flow ...`
(also `... cannot deliver pressure` for PRV/PSV) when a valve is in an
internal XFCV/XPRESSURE state.  Pumps have `EN_PUMP_STATE` (exposing
XHEAD/XFLOW), but valves have no equivalent: `EN_getlinkvalue(EN_STATUS)`
maps *any* internal status above OPEN to "active", making an XFCV valve
indistinguishable from a normally active one.

This is the only reason 4 of the 59 corpus networks
(`simpson_test`, `simpson_test_2-12`, `2fcvs`, `5fcvs`) don't replicate.

*Suggested API:* `EN_VALVE_STATE` link property mirroring `EN_PUMP_STATE`.

## 4. Warnings are reported as a single code per step  (partial WORKAROUND)

The report interleaves warning lines (WARN01..WARN06 in `src/text.h`)
between "Analysis begun/ended".  `EN_runH()` returns only one numeric code
per step even when several conditions fire, and nothing identifies the
affected elements.  `probeWarnings()` in `collect.c` reconstructs what it
can by probing the API after each warned step, mirroring `writehydwarn()`:

| Warning | Reconstructable? | How / why not |
|---------|------------------|---------------|
| WARN01 system unbalanced          | yes | `EN_getstatistic(EN_ITERATIONS/EN_RELATIVEERROR)` vs `EN_TRIALS`/`EN_ACCURACY`; `EN_getoption(EN_UNBALANCED) == -1` adds "EXECUTION HALTED." |
| WARN02 max trials, maybe unstable | yes | same statistics |
| WARN03a/b/c disconnected nodes    | **no** | needs the engine's connectivity walk: which nodes are cut off and which closed link caused it |
| WARN04 pump cannot deliver head/flow | yes | `EN_getlinkvalue(EN_PUMP_STATE)` = XHEAD/XFLOW per pump |
| WARN05 valve in abnormal state    | **no** | gap #3 |
| WARN06 negative pressures         | yes | scan junctions for pressure < 0 and demand > 0 (DDA only) |

*Suggested API:* a per-step warning enumerator
(`EN_getwarningcount`/`EN_getwarning(i, *code, *elementType, *elementIndex)`)
or a warning callback, so a caller need not re-derive the engine's checks.

## 5. TRACE quality column metadata  (WORKAROUND, minor)

For a TRACE analysis the report titles the node-table quality column
`% from` over the trace node's ID (`src/input3.c` sets
`ChemName="% from"`, `ChemUnits=<trace node>`), while the summary's
"Water Quality Tolerance" line uses `% from` as its units
(`Field[QUALITY].Units`, `src/input1.c`).  `EN_getqualinfo()` instead
returns the generic pair `"TRACE"` / `"% from"` - neither string pair the
report prints.  Reconstructable from `EN_getqualtype` + `EN_getnodeid`, but
the API's name/units do not match what the report actually uses.

## 6. Unit headloss for pipes  (WORKAROUND, minor)

The link table reports headloss per 1000 length units for pipes (computed
in `linkoutput()`, `src/output.c`).  `EN_getlinkvalue(EN_HEADLOSS)` returns
*total* headloss.  Computable as `1000 * headloss / EN_LENGTH`, but there is
no direct "unit headloss" property matching the reported quantity.

## 7. Reaction rate and friction factor have no property codes  (GAP, latent)

The output file stores 8 link variables; the last two - average reaction
rate and friction factor - have **no** `EN_getlinkvalue` codes.  They only
appear in reports when the non-default `[REPORT]` fields are enabled (no
corpus network does), so this doesn't bite yet; the replica would print
zeros.  *Suggested API:* `EN_REACTIONRATE`, `EN_FRICTIONFACTOR` link
properties.

## 7b. Report-unit conventions the caller must re-derive  (informational)

The summary prints the hydraulic timestep and total duration in *minutes*
when the hydraulic step is under half an hour, otherwise hours
(`initunits()`, `src/input1.c`).  `EN_gettimeparam` returns seconds, so this
is replicable, but the convention lives only in engine code.  The same holds
for the per-1000-length unit-headloss convention (#6) and the REAL4
storage (#8).

## 8. Precision-level divergences  (informational)

* The native tables print values stored as REAL4 (single precision) in the
  binary output file; `EN_getnodevalue`/`EN_getlinkvalue` return doubles.
  The replica casts to `float` before formatting to match.  Statistics
  (`STATISTIC AVERAGE` etc.) must likewise be accumulated in `float`, with
  `savetimestat()`'s special cases: link flows averaged as absolute values,
  link status collapsed to open/closed before averaging.
* For closed links the output file stores the raw internal flow (a tiny
  non-zero number) while `EN_FLOW` returns exactly 0.  Not observed to
  change any printed digit across the corpus.

## 9. Sequencing constraints  (informational)

* The summary block is written *during* `EN_open()`, so its "Reporting
  Criteria" lines snapshot the INP's settings; `EN_setreport` calls made
  afterwards affect the tables but not the already-written summary.  The
  replica mirrors this (see `summaryNodeRptFlag`).
* Everything in gaps #2 and #4 requires the step-by-step
  `EN_openH/EN_runH/EN_nextH` + `EN_openQ/EN_runQ/EN_nextQ` loops; a caller
  using the one-shot `EN_solveH`/`EN_solveQ` cannot collect that data at
  all.

## 10. Still to examine: STATUS YES / FULL  (second pass)

The hydraulic status report (`writehydstat`, `writestatchange`,
`writecontrolaction`, `writeruleaction`, `writerelerr`, flow/mass balance
blocks) needs per-event data the API does not expose: per-step status-change
events with old/new state, tank filling/emptying transitions, which
control/rule fired a change, per-trial convergence traces (FULL), and the
flow-balance / water-quality mass-balance summaries.  `repgen --status
yes|full` already runs and produces both reports so the gaps can be measured
when this pass starts.
