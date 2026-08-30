# Proposed toolkit API additions

What the EPANET toolkit would need so a program can follow what a network is
doing **at run time** without re-implementing engine internals.

Derived from `MISSING_API.md`, which records what it actually took to
reproduce EPANET's own report from public API calls alone.

**Inclusion rule.** Only things that are either never obtainable, or
obtainable today only by copying engine logic that could silently change.
Anything a caller can compute with simple arithmetic over existing getters
is deliberately left out — see *Considered and rejected* at the end.

**Complexity** is the cost of adding it to the engine:
`trivial` = expose a value the engine already holds ·
`small` = a getter plus somewhere to keep the value ·
`moderate` = new bookkeeping across a simulation phase.

---

## Tier 1 — Runtime state and events

The happy path: what the network is doing while it runs. Everything here is
either invisible today or reachable only by duplicating solver logic.

### 1. Control and rule action events
Report which control or rule changed which link, when, and to what.
**Importance** critical · **Complexity** moderate
*Why:* the only alternative is re-implementing `controls()` **and** `pswitch()`
— junction pressure controls fire inside the solver, after the solution, and
are reported only at STATUS FULL — plus the entire rule premise evaluator.

```c
typedef enum {
  EN_ACTION_CONTROL = 0,   //!< a simple control fired
  EN_ACTION_RULE    = 1    //!< a rule action was taken
} EN_ActionType;

int EN_getactioncount(EN_Project ph, int *out_count);
int EN_getaction(EN_Project ph, int index, int *out_actionType,
                 int *out_actionIndex, int *out_linkIndex, long *out_time,
                 int *out_newStatus, double *out_newSetting);
```
Actions accumulate during `EN_runH` (controls) and `EN_nextH` (rules), and
reset at the start of each.

### 2. Raw link state
Expose the internal link state that `EN_STATUS` collapses away.
**Importance** critical · **Complexity** trivial
*Why:* `EN_STATUS` maps everything above OPEN to "active", so a valve that
cannot deliver flow is indistinguishable from one working normally; today the
raw code is reachable only through an undocumented quirk of `EN_PUMP_STATE`.

```c
typedef enum {
  EN_LS_XHEAD      = 0,  //!< closed, cannot deliver head
  EN_LS_TEMPCLOSED = 1,  //!< temporarily closed by the solver
  EN_LS_CLOSED     = 2,
  EN_LS_OPEN       = 3,
  EN_LS_ACTIVE     = 4,  //!< valve regulating
  EN_LS_XFLOW      = 5,  //!< open, exceeds maximum flow
  EN_LS_XFCV       = 6,  //!< open, cannot deliver flow
  EN_LS_XPRESSURE  = 7   //!< open, cannot deliver pressure
} EN_LinkState;

// new EN_LinkProperty code, read with EN_getlinkvalue
EN_LINK_STATE = 30
```

### 3. Tank state
Report whether a tank is filling, emptying, overflowing or idle.
**Importance** high · **Complexity** small
*Why:* the engine's rule is a threshold on net inflow in **internal** units
plus a level-versus-maximum test; reproducing it means copying two magic
constants and a unit conversion that could change without notice.

```c
typedef enum {
  EN_TS_CLOSED      = 0,
  EN_TS_FILLING     = 1,
  EN_TS_EMPTYING    = 2,
  EN_TS_OVERFLOWING = 3
} EN_TankState;

// new EN_NodeProperty code, read with EN_getnodevalue
EN_TANK_STATE = 33
```

### 4. Solver failure and convergence detail
Name the elements behind the convergence statistics and a failed solve.
**Importance** high · **Complexity** trivial
*Why:* the engine already computes these indexes each trial and keeps the
values; only the identities are withheld, so a caller sees *how bad* the error
is but never *where*. The same blind spot hides `badvalve()`, which resolves an
ill-conditioned matrix by **forcing** a control valve's status — a state change
the engine makes on the caller's behalf and never reports.

```c
// new EN_AnalysisStatistic codes for EN_getstatistic
EN_MAXHEADERRORLINK   = 8,   //!< link carrying the largest head loss error
EN_MAXFLOWCHANGELINK  = 9,   //!< link with the largest flow change
EN_MAXFLOWCHANGENODE  = 10,  //!< node with the largest flow change
EN_ILLCONDITIONEDNODE = 11   //!< node that made the solve fail (error 110)
```

### 5. Time step attribution
Say what ended the hydraulic step that just finished.
**Importance** high · **Complexity** small
*Why:* `EN_timetonextevent` predicts from only three of the six determinants
`timestep()` applies — it ignores pattern-period and report-time truncation and
rule evaluation — so it can promise a longer step than the engine then takes,
and never reports the report-time or rule causes its own enum already defines.

```c
// what ended the step just taken, read after EN_nextH
int EN_getstepcause(EN_Project ph, int *out_eventType, int *out_elementIndex);

// EN_TimestepEvent gains the causes timestep() can actually apply
EN_STEP_PATTERN = 5,   //!< a demand pattern period boundary
EN_STEP_RULE    = 6    //!< a rule-based control fired
```

### 6. Solver iteration callback
Observe convergence trial by trial instead of only the final result.
**Importance** medium · **Complexity** moderate
*Why:* nothing inside a hydraulic solve is observable — a caller cannot tell
a run that converged immediately from one that nearly failed.

```c
int EN_setsolvercallback(EN_Project ph,
        void (*callback)(void *userData, int trial, double relativeError),
        void *userData);
```

---

## Tier 2 — Errors and events on the unhappy path

Raised during a run or when opening a project. Each is currently either
unnamed, or reported as a single code that says nothing about the element.

### 7. Per-step warnings
List every warning condition raised by a hydraulic step, with the element.
**Importance** high · **Complexity** moderate
*Why:* `EN_runH` returns one code even when several conditions fire, and
never says which pump, valve or node caused it.

```c
int EN_getwarningcount(EN_Project ph, int *out_count);
int EN_getwarning(EN_Project ph, int index, int *out_code,
                  int *out_objectType, int *out_objectIndex);
```

### 8. Disconnected nodes
Name the nodes cut off from any source, and the link responsible.
**Importance** medium · **Complexity** small
*Why:* rebuilding this means copying the seeding rule, the one-way test on
check valves and PRV/PSVs, **and** the adjacency ordering — the answer to
"which link" depends on the order the engine happens to build its lists in.

```c
int EN_getdisconnectedcount(EN_Project ph, int *out_count);
int EN_getdisconnectednode(EN_Project ph, int index, int *out_nodeIndex);
int EN_getdisconnectinglink(EN_Project ph, int *out_linkIndex);
```

### 9. Validation errors naming their elements
Say which tank, pump, pattern or curve failed validation.
**Importance** medium · **Complexity** moderate
*Why:* `EN_openH` returns a single code for the whole network; recovering the
culprit means re-implementing every check, including the pump curve fit.

```c
int EN_getvalidationerrorcount(EN_Project ph, int *out_count);
int EN_getvalidationerror(EN_Project ph, int index, int *out_code,
                          int *out_objectType, int *out_objectIndex);
```

### 10. Non-fatal open warnings
Surface problems that `EN_open` currently swallows.
**Importance** medium · **Complexity** trivial
*Why:* a missing saved-hydraulics file is written into the report, silently
downgraded to a scratch file, and `EN_open` still returns success — no caller
can detect it by any means.

```c
int EN_getopenwarningcount(EN_Project ph, int *out_count);
int EN_getopenwarning(EN_Project ph, int index, int *out_code);
```

### 11. Input and rule parse diagnostics
Report which line of the input file failed to parse and why.
**Importance** low · **Complexity** moderate
*Why:* `EN_open` returns only "one or more errors in input file" and leaves
the project closed, discarding the section, token and line it already knew.

```c
int EN_getparseerrorcount(EN_Project ph, int *out_count);
int EN_getparseerror(EN_Project ph, int index, int *out_code,
                     int *out_sectionType, int *out_lineNumber,
                     char *out_line);   // out_line: EN_MAXMSG+1 chars
```

---

## Tier 3 — End-of-run accounting

Lower priority: these are summaries produced once the run finishes, not
runtime behaviour. Listed because each is either impossible to reconstruct
or requires copying the engine's accumulation exactly.

### 12. Cumulative pump energy
Return each pump's end-of-run energy statistics and the system peak demand.
**Importance** low · **Complexity** small
*Why:* the API exposes only instantaneous kW, so the totals must be
re-integrated step by step — copying the zero-flow clamp, the price-pattern
index arithmetic and the single-period special case — and that is impossible
altogether after a one-shot `EN_solveH`.

```c
typedef enum {
  EN_PUMP_TIMEONLINE = 0,  //!< % of the simulation the pump ran
  EN_PUMP_AVGEFFIC   = 1,  //!< average efficiency, %
  EN_PUMP_KWHPERFLOW = 2,  //!< kWh per Mgal or per m3
  EN_PUMP_AVGKW      = 3,
  EN_PUMP_PEAKKW     = 4,
  EN_PUMP_COSTPERDAY = 5
} EN_PumpEnergyProperty;

int EN_getpumpenergy(EN_Project ph, int linkIndex, int property,
                     double *out_value);

// new EN_AnalysisStatistic codes
EN_PEAKENERGY   = 12,  //!< peak total pumping power, kW
EN_ENERGYCHARGE = 13   //!< demand charge for that peak
```

### 13. System flow balance
Return where the water went over the run: inflow, demand, leakage, storage.
**Importance** low · **Complexity** trivial
*Why:* the engine already keeps this as a finalized struct; a caller can only
match it by weighting every step by the *next* step's length and normalising
by the final time.

```c
typedef enum {
  EN_FB_TOTALINFLOW    = 0,
  EN_FB_CONSUMERDEMAND = 1,
  EN_FB_DEFICITDEMAND  = 2,
  EN_FB_EMITTERFLOW    = 3,
  EN_FB_LEAKAGEFLOW    = 4,
  EN_FB_TOTALOUTFLOW   = 5,
  EN_FB_STORAGEFLOW    = 6,
  EN_FB_RATIO          = 7
} EN_FlowBalanceProperty;

int EN_getflowbalance(EN_Project ph, int property, double *out_value);
```

### 14. Water quality mass balance
Return the mass accounting for a quality run, not just its ratio.
**Importance** low · **Complexity** trivial
*Why:* the mass terms are sums over the solver's internal pipe-segment lists,
which no API reaches — they cannot be recomputed at any cost.

```c
typedef enum {
  EN_MB_INITIAL  = 0,
  EN_MB_INFLOW   = 1,
  EN_MB_OUTFLOW  = 2,
  EN_MB_REACTED  = 3,
  EN_MB_FINAL    = 4,
  EN_MB_RATIO    = 5,
  EN_MB_SEGCOUNT = 6   //!< number of water quality segments
} EN_MassBalanceProperty;

int EN_getmassbalance(EN_Project ph, int property, double *out_value);
```

### 15. Per-link reaction rate
Return each link's average reaction rate during a quality run.
**Importance** low · **Complexity** trivial
*Why:* the engine computes and stores it per link, but no property code
reads it, and it depends on segment data a caller cannot see.

```c
// new EN_LinkProperty code, read with EN_getlinkvalue
EN_REACTIONRATE = 31
```

---

## Considered and rejected

Reachable today with simple arithmetic or a remembered value — not worth an
API:

| Wanted | Get it by |
|---|---|
| Unit head loss (per 1000 length) | `EN_HEADLOSS / EN_LENGTH` |
| Link status *transitions* | remembering the previous `EN_LINK_STATE` (#2) |
| Friction factor | arithmetic over head loss, flow, diameter and length (needs the unit conversions) |
| Pressure-deficient node counts | `EN_getstatistic(EN_DEFICIENTNODES / EN_DEMANDREDUCTION)` |
| Where water goes per node, per step | `EN_DEMANDFLOW`, `EN_EMITTERFLOW`, `EN_LEAKAGEFLOW`, `EN_FULLDEMAND`, `EN_DEMANDDEFICIT` — already complete |
| Report formatting settings | only affects report layout, not network behaviour |
