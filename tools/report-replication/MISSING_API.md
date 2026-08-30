# Toolkit API gaps found by replicating the EPANET report

Goal: reproduce the engine's native `.rpt` report 1:1 using **only** public
toolkit API calls (`include/epanet2_2.h`), so the same data could later be
rendered in any other format (HTML, JSON, ...).  `repgen` writes the native
report and a pure-API replica side by side; `compare-reports.sh` diffs them
(ignoring wall-clock timestamps) across the three bundled example networks
and the [epanet-example-networks](https://github.com/OpenWaterAnalytics/epanet-example-networks)
regression corpus (59 networks total).

**Status**

| Report level | Result over the 59 networks |
|--------------|-----------------------------|
| `STATUS NO`  | **59 / 59 byte-identical** (also with `NODES ALL`, `LINKS ALL`, `ENERGY YES` forced on, and on the 65-network `make-variants.sh` corpus) |
| `STATUS YES` | **46 / 59 byte-identical**; the other 13 differ *only* by the six water quality mass-balance lines of gap #11. On the variant corpus (which adds PDA, timer/clocktime controls, pagination, AGE quality and priced energy): 46 identical, 19 mass-balance-only, 0 mismatched |
| `STATUS FULL`| not implemented - see #12 |

Getting there required every workaround documented below: the gaps are real
even where the replication now succeeds.

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
(`inp_sniff.c`), duplicating `reportdata()` from `src/input3.c`.  For
settings that act after `EN_open`, `EN_resetreport()` + `EN_setreport()`
can impose a *known* state instead - but that cannot retroactively fix the
summary block, which `EN_open` has already written.

Related setter-side quirk: in the `EN_setreport` grammar the word `QUALITY`
always resolves to the *node* quality field (first match), so the link
quality column cannot be addressed by name at all.

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

## 3. Abnormal valve states: only via an undocumented quirk  (WORKAROUND)

`writehydwarn()` emits `WARNING: FCV 47 open but cannot deliver flow ...`
(also `... cannot deliver pressure` for PRV/PSV) when a valve is in an
internal XFCV/XPRESSURE state.  The *documented* API cannot see this:
`EN_getlinkvalue(EN_STATUS)` maps any internal status above OPEN to
"active", making an XFCV valve indistinguishable from a normally active
one.  Pumps have `EN_PUMP_STATE`; valves have no documented equivalent.

*Workaround used:* `EN_getlinkvalue(EN_PUMP_STATE)` on a **non-pump** link
happens to return the raw internal `LinkStatus` code (0..10) because the
implementation only refines the value for pumps - so `6`/`7` reveal
XFCV/XPRESSURE.  This behavior is undocumented and could change; 4 corpus
networks (`simpson_test`, `simpson_test_2-12`, `2fcvs`, `5fcvs`) replicate
only because of it.

*Suggested API:* an official `EN_VALVE_STATE` link property mirroring
`EN_PUMP_STATE` (or documenting/renaming the current behavior).

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
| WARN03a/b/c disconnected nodes    | **no** | needs the engine's connectivity walk; a caller-side flood fill over `EN_getlinknodes` is possible but WARN03c's "because of Link X" depends on the engine's adjacency-list ordering, so even a re-implementation may name a different (equally valid) link |
| WARN04 pump cannot deliver head/flow | yes | `EN_getlinkvalue(EN_PUMP_STATE)` = XHEAD/XFLOW per pump |
| WARN05 valve in abnormal state    | via quirk | `EN_PUMP_STATE` on the valve link returns the raw status code (gap #3's undocumented workaround) |
| WARN06 negative pressures         | yes | scan junctions for pressure < 0 and demand > 0 (DDA only) |
| FMT62 "System ill-conditioned at node X" | **no** | written when the hydraulic solve fails (error 110); the offending node index is not exposed - suggest `EN_getstatistic(EN_ILLCONDITIONEDNODE)` |

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
zeros.  The friction factor can *almost* be re-derived from headloss,
length, diameter and velocity, but with extra rounding relative to the
engine's internal computation; the average reaction rate cannot be
recomputed at all (it needs internal pipe-segment quality data).
*Suggested API:* `EN_REACTIONRATE`, `EN_FRICTIONFACTOR` link properties.

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

## Engine bugs found while replicating

* **Demand Charge is applied twice in the report.**  `savenergy()`
  (`src/output.c`) scales `hyd->Emax` by the demand-charge rate when writing
  the binary file's energy epilog; `writeenergy()` (`src/report.c`) then
  prints `Emax * Dcost` - multiplying by the rate a second time.  With a
  demand charge of 12.5 on Net1 the report shows 15110.49 instead of the
  correct 1208.84 (the binary output file holds the correct value).
  Invisible whenever the rate is 0, which is why no test network catches
  it.  The replica mirrors the doubled value for byte-parity but stores the
  correct one in its data model.

## 10. The hydraulic status report has no event stream  (WORKAROUND)

`STATUS YES` interleaves solver events into the report as the run proceeds.
The API reports no events at all, so the replica re-derives each line by
probing state around the `EN_runH` / `EN_nextH` boundary and keeping the
same memories the engine keeps internally.  What that costs, line by line:

| Status line | How the replica produces it |
|-------------|-----------------------------|
| FMT58/59 "Balanced/Unbalanced after N trials" | clean: `EN_getstatistic(EN_ITERATIONS / EN_RELATIVEERROR)` after `EN_runH` |
| FMT69a/b PDA demand reduction | clean: `EN_getstatistic(EN_DEFICIENTNODES / EN_DEMANDREDUCTION)` |
| FMT50/51 tank/reservoir filling/emptying/overflowing | no tank-status property: the caller must re-implement the state machine (net inflow thresholded at 0.001 **cfs** - so `EN_DEMAND` must be converted back from flow units - plus an `EN_MAXLEVEL` overflow test in feet) *and* seed its own memory to `TEMPCLOSED`, an internal detail of `inithyd()` |
| FMT52/53 link status changes | needs the RAW internal status code, which only the gap #3 quirk provides; `EN_STATUS` is too coarse. For pumps the quirk's XFLOW/XHEAD refinement must be undone (`EN_STATUS >= 1` means raw OPEN) |
| FMT54/55 control actions | **no control-fired notification**: the replica re-implements `controls()` - including `tankvolume()` over `EN_VOLCURVE` / `EN_TANKDIAM` / `EN_MINVOLUME` / `EN_MINLEVEL` - and evaluates it against cached state. See the sequencing trap below |
| FMT63 rule actions | not implemented; no rule-firing notification exists, and rule actions fire at sub-step times. No corpus network emits one, so this is untested |
| Hydraulic Flow Balance block | reproducible but laborious: per-step accumulation of `EN_DEMANDFLOW` / `EN_EMITTERFLOW` / `EN_LEAKAGEFLOW` / `EN_FULLDEMAND` / tank `EN_DEMAND`, weighted by the *next* step length and divided by the final time - suggest exposing `hyd->FlowBalance` directly |

**Sequencing trap (undocumented).**  `controls()` runs at the start of
`EN_runH`, *before* the solve, and the tank heads it sees were updated at
the end of the *previous* `EN_nextH` (`tanklevels()` inside `timestep()`).
A caller that caches state after `EN_runH` - the intuitive choice - gets
heads from the wrong instant and never fires a level control.  The state
must be captured after `EN_nextH`.  Nothing in the API documents this.

*Suggested API:* a status/event callback (or a per-step event queue)
reporting control firings, rule firings and status transitions with the
element index and time, mirroring what `EN_setreportcallback` does for text.

## 11. Water quality mass balance is unobtainable  (GAP - blocks 13 networks)

`STATUS YES` ends with a Water Quality Mass Balance block: Initial Mass,
Mass Inflow, Mass Outflow, Mass Reacted, Final Mass, Mass Ratio and Total
Segments.  Only **Mass Ratio** is exposed, via
`EN_getstatistic(EN_MASSBALANCE)`.  The rest are accumulated inside the
quality solver, and the initial/final masses are sums over the per-link
water-quality **segment** lists (`findstoredmass()` in `src/quality.c`), a
structure the API does not expose in any form - so they cannot be
recomputed either.  This is the only reason 13 of the 59 networks do not
replicate at `STATUS YES`.

Knock-on effect: because the replica omits six lines the engine prints, any
network that also paginates (`PAGE n`) gets its page breaks shifted, turning
one gap into a whole-file mismatch.

*Suggested API:* `EN_getmassbalance(ph, code, *value)` for the five mass
terms plus a segment count (or extra `EN_getstatistic` codes).

## 12. Still to examine: STATUS FULL  (next pass)

`STATUS FULL` adds a per-trial solver trace that the API cannot see at all.
Measured against the 32 corpus networks whose INP files request it, exactly
these line kinds are missing:

| Line | Blocking gap |
|------|--------------|
| FMT64 "Balancing the network:" + FMT65 "Trial N: relative flow change = x" | per-iteration convergence values; `EN_getstatistic(EN_RELATIVEERROR)` only reports the final trial - suggest a solver iteration callback |
| FMT67/68 "maximum flow change = x for Link Y" / "maximum head error = x for Link Y" | `EN_getstatistic(EN_MAXFLOWCHANGE / EN_MAXHEADERROR)` give the values but not the elements - suggest `EN_MAXFLOWCHANGELINK` / `EN_MAXHEADERRORLINK` |
| FMT56/57 intra-iteration setting/status switches | invisible to the API - same solver callback |
| FMT61 valve ill-conditioning | no event |

Everything else in those reports already matches, so closing the four rows
above would finish `STATUS FULL`.
