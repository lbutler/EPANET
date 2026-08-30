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
| validation-error networks | reproduced byte-for-byte - bad tank levels, pump curves, patterns, curves and unlinked junctions (#13); input and rule parse errors are not (#14) |
| `STATUS FULL`| **59 / 59 reach "api-gap only"**: every line the replica cannot produce is one the API provably cannot supply (the solver trace of #12 plus #11), and the replica invents nothing |

Getting there required every workaround documented below: the gaps are real
even where the replication now succeeds.

**The API's real boundary is data, not text.**  `EN_setreportcallback`
intercepts `writeline()`, so a caller can capture *every* line the engine
writes - including the STATUS FULL solver trace and the mass balance it
cannot otherwise reach - as pre-formatted strings.  What it cannot get is
the *data* behind those strings: which link, which trial, what value.  A
consumer that wants the report in another format (HTML, JSON, a table) is
therefore forced to re-parse EPANET's fixed-width text.  Two further
catches: installing the callback **diverts** output, so the report file is
left with only the pre-callback logo (measured: 486 bytes on Net1), and the
callback receives lines without `writeline()`'s `"\n  "` prefix while some
formats carry their own embedded newlines.  Every gap below should be read
as "not available as data".

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

On that same failure path `writehyderr()` calls `writehydstat(pr, 0, 0)`,
which emits the tank/link status block **without** a Balanced/Unbalanced
line.  A caller cannot detect this: `EN_getstatistic(EN_ITERATIONS)` still
reports a non-zero trial count, so gating the line on that statistic
produces a line the engine did not write.  Combined with the unexposed
FMT62 node, a failed-solve report cannot be reproduced.

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
| FMT63 rule actions | not implemented - see the rule note below. No corpus network emits one |
| Hydraulic Flow Balance block | reproducible but laborious: per-step accumulation of `EN_DEMANDFLOW` / `EN_EMITTERFLOW` / `EN_LEAKAGEFLOW` / `EN_FULLDEMAND` / tank `EN_DEMAND`, weighted by the *next* step length and divided by the final time - suggest exposing `hyd->FlowBalance` directly |

**Rule actions (FMT63) are only half-observable.**  They are emitted from
inside `EN_nextH` (via `timestep()` -> `ruletimestep()` -> `checkrules()`),
stamped with the *next* period's clock time - so in the report they sit
after the current period's blank line and before the next period's control
lines.  A caller can detect *that* rules fired, by diffing each link's raw
status/setting across `EN_nextH`, but cannot recover:
* **which rule** fired - there is no "rule fired" query, and re-deriving it
  means re-implementing the premise evaluator; premises of the form
  `IF SYSTEM DEMAND ...` are not even re-derivable, because the engine's
  internal system-demand accumulation differs from what `EN_FULLDEMAND`
  exposes;
* **the order** of several actions taken at the same instant, which the
  engine emits by descending rule index after a priority-based replacement
  pass - nothing in the API reports that list.

Do not reach for `EN_LINK_INCONTROL` / `EN_NODE_INCONTROL` here: both are
answered by `incontrols()` (`src/project.c`), a purely *structural* scan of
the control and rule definitions.  They return 1 for any element merely
*mentioned* by a control or rule and never change during a run, so they say
nothing about whether anything fired.  (The rule-inspection getters
themselves are sound: `EN_getpremise` was verified to return object,
variable and operator codes that do match the public `EN_RuleObject` /
`EN_RuleVariable` / `EN_RuleOperator` enums.)

*Suggested API:* a rule-action event callback reporting (rule index, link
index, time) as each action is taken.

**`EN_getcontrol` cannot express a control's status.**  It returns the
control's *setting*, using the sentinels `EN_SET_OPEN`/`EN_SET_CLOSED` only
when the stored setting is `MISSING`.  For a **GPV** control, `controldata()`
(`src/input3.c`) stores `setting = Link.Kc` for *both* `OPEN` and `CLOSED`,
so the sentinel never appears and OPEN is indistinguishable from CLOSED -
the replica cannot decide whether the control fires, making its FMT54/55
line unreproducible.  (For pumps and pipes the status is recoverable,
because a numeric setting of 0 means CLOSED and anything else OPEN.)  No
corpus network has a controlled GPV, so this is untested but real.
*Suggested API:* have `EN_getcontrol` return the control's status alongside
its setting.

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

## 12. STATUS FULL: the solver trace is invisible to the API  (GAP)

`STATUS FULL` is `STATUS YES` plus a per-trial solver trace.  Everything
`STATUS YES` covers still replicates at FULL, and running the whole corpus
at `--status full` classifies all 59 networks as *api-gap only* with **no
replica-invented lines** - so the trace below is precisely, and only, what
is missing.  Every one of these lines is written from inside a single
`EN_runH` call, between the solver's iterations, and the toolkit exposes no
solver callback or iteration hook of any kind (the only callback is
`EN_setreportcallback`, which yields formatted text - see the note at the
top of this file).  Measured over the corpus:

| Line | Blocking gap |
|------|--------------|
| FMT64 "Balancing the network:" + FMT65 "Trial N: relative flow change = x" | per-iteration convergence values; `EN_getstatistic(EN_RELATIVEERROR)` only reports the final trial - suggest a solver iteration callback |
| FMT67/68 "maximum flow change = x for Link Y" / "maximum head error = x for Link Y" | `EN_getstatistic(EN_MAXFLOWCHANGE / EN_MAXHEADERROR)` give the values but not the elements - suggest `EN_MAXFLOWCHANGELINK` / `EN_MAXHEADERRORLINK` |
| FMT56/57 intra-iteration setting/status switches | invisible to the API - same solver callback |
| FMT61 valve ill-conditioning | no event; not triggered by any corpus network |

Empirical tally of the missing lines across the corpus at `--status full`
(from `line-coverage.sh`): 4 440 `Trial N: relative flow change`, 1 804
`maximum head error ... for Link`, 1 801 `maximum flow change ... for
Link`, 3 the `for Node` variant, 1 795 `Balancing the network:`, and 70
`<type> <id> switched from A to B`.  No `setting changed to` (FMT56) or
`caused ill-conditioning` (FMT61) line is exercised by any corpus network,
so those two remain untested.

One near-miss worth noting: the *values* in FMT66/67/68 are exposed -
`EN_getstatistic(EN_MAXFLOWCHANGE / EN_MAXHEADERROR)` returns exactly the
`hbal->maxflowchange`/`maxheaderror` those lines print (`src/hydsolver.c`
assigns `hyd->MaxFlowChange`/`MaxHeadError` from the same struct).  Only the
**element indexes** `maxflowlink` / `maxflownode` / `maxheadlink` are
withheld, and `reporthydbal()` can fire more than once per step.  Adding
`EN_MAXFLOWCHANGELINK` / `EN_MAXHEADERRORLINK` statistics would close two of
the four rows on its own.

Closing the four rows above would finish `STATUS FULL`.

## 13. Validation errors name elements the API never identifies  (WORKAROUND)

`validateproject()` (`src/validate.c`) runs inside `EN_openH` and writes one
line per offending element before failing:

```
  Error 225: invalid lower/upper levels for tank node 2
  Error 227: invalid head curve for pump 9
  Error 230: nonincreasing x-values for curve 1
  Error 110: cannot solve network hydraulic equations
```

`EN_openH` returns a single code (110) and nothing about *which* tank, pump,
pattern or curve was at fault.  The replica reproduces these lines by
re-implementing every check - tank level and volume-curve range, pump head
curve (including the power-function fit of `powerfuncpump()` and the
monotonic-head test of `customcurvepump()`), empty patterns, empty and
non-monotonic curves - in `validateproject()`'s exact emission order
(tanks, pumps, patterns, curves).  All of the inputs are readable, so this
works, but it is another wholesale duplication of engine logic.
Two smaller wrinkles found here:

* `EN_geterror` composes `"Error <code>: " + message`.  The report's own
  lines build that prefix themselves, so reproducing them means stripping
  the prefix back off the API's output - there is no getter for the bare
  message text.
* `validatepatterns()` loops from pattern index **0**, which the API cannot
  address (`EN_getpatternlen` rejects index 0), so an empty pattern 0 would
  be missed.

`unlinked()` (`src/project.c`) is a second such check, running in the same
`EN_openH` after validation passes: it names up to ten junctions no link
connects to (`Error 234`) before failing with 233.  The replica reproduces
those too, by rebuilding adjacency from `EN_getlinknodes`.

*Suggested API:* have `EN_openH` (or a follow-up query) enumerate the
offending elements, e.g. `EN_getvalidationerror(i, *code, *objType, *index)`.

## 14. Input-file parse diagnostics are unreachable  (GAP)

When the input file cannot be read cleanly, `src/input2.c` writes a
diagnostic *pair* per bad line - the error with its token and section, then
an echo of the offending input line itself:

```
  Error 201: syntax error  in [PATTERNS] section:
   EMPTYPAT

  Error 200: one or more errors in input file
```

`EN_open` returns only 200, and leaves the project closed, so a pure-API
caller has no access to the section, the token, the line number or the text
of the line.  The replica can reproduce the logo and the terminal
`Error 200` line and nothing else; the diagnostic pairs are simply absent.
(`EN_openX` opens the project anyway and would at least allow the rest of
the report to be built, but it still does not expose the diagnostics.)

Malformed **rules** add a second, differently-shaped block, because
`ruleerrmsg()` (`src/rules.c`) reports the clause itself before the generic
handler reports the line again:

```
  Input Error 201: syntax error in following line of Rule BAD1:
  IF TANK 2 LEVEL BOGUSOP 110
  Error 200: one or more errors in input file  in [RULES] section:
  IF TANK 2 LEVEL BOGUSOP 110

  Error 200: one or more errors in input file
```

(The doubled space in the third line is the engine's own formatting: the
generic handler prints `"Error %d: %s %s in %s section:"` with an empty
token argument.)  All five lines are equally unreachable.

*Suggested API:* an error-list query after a failed open, e.g.
`EN_getparseerrorcount` / `EN_getparseerror(i, *code, *section, char *line)`.
