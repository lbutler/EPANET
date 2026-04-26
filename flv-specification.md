# Float Valve (FLV) — EPANET Implementation Specification

## 1. Overview

A new EPANET valve type, **FLV** (Float Valve), for modelling tank inlet valves whose opening is governed by the level of the tank they feed. The valve modulates continuously: fully open when the tank is well below its top water level (TWL), fully closed when the tank reaches TWL, with a linear or curve-defined opening profile across a regulating band immediately below TWL.

The TWL is taken implicitly as the controlling tank's `MaxLevel`. The only FLV-specific parameter is the regulating range — the depth of the modulating band below TWL.

**FLV is structurally identical to PCV.** The only divergence is the per-step computation:

| Aspect                             | PCV                                 | FLV                                                |
| ---------------------------------- | ----------------------------------- | -------------------------------------------------- |
| INP token layout                   | `... PCV Setting MinorLoss [Curve]` | `... FLV Setting MinorLoss [Curve]`                |
| `Setting` semantics                | percent open (0–100)                | regulating range (length units)                    |
| `pctOpen` source                   | user-supplied directly              | computed from `MaxLevel`, current depth, `Setting` |
| Setting valid range                | [0, 100]                            | (0, MaxLevel − MinLevel]                           |
| Status (OPEN/CLOSED/ACTIVE)        | identical                           | identical                                          |
| `[CONTROLS]` / `[RULES]` targeting | supported                           | supported                                          |
| Mid-simulation API mutation        | supported                           | supported                                          |
| `EN_saveinpfile` round-trip        | identical layout                    | identical layout                                   |

The loss coefficient is computed identically to PCV:

```
K_active = K_open / ( f(pctOpen) )^2
```

`pctOpen` is sampled once at the start of each hydraulic timestep and held constant through the step.

The on/off altitude valve case can already be modelled in EPANET using simple controls; this specification deliberately does not duplicate that capability.

---

## 2. Goals and Non-Goals

### Goals

- Add FLV as a new valve type recognized in `[VALVES]`.
- Reuse PCV's INP layout, API surface, controls/rules path, and loss-coefficient calculation.
- Implicit controlling tank: FLV's `Node2` is the controlling tank.
- Implicit close-point: tank's `MaxLevel`.
- `Setting` field carries the regulating range; valid range is `(0, MaxLevel − MinLevel]`.
- Optional valve characteristic curve; defaults to linear.
- Full support for `[CONTROLS]`, `[RULES]`, and mid-simulation API mutation of Setting / Status / Curve / MinorLoss — same semantics as PCV.
- `[STATUS] OPEN` / `[STATUS] CLOSED` / `[STATUS] ACTIVE` behave the same as for PCV: lock open, lock closed, regulating.
- INP round-trip: every FLV property must survive `EN_saveinpfile`.
- Smallest-possible diff to the C source. Match existing code style. No new comments beyond what existing code uses.
- Tests that the implementing AI can run and verify against numerical expectations.

### Non-Goals (deferred)

- User-facing documentation (will be coordinated with OWA after the API is settled).
- ONOFF / hysteretic behaviour. Use existing simple controls for that case.
- Explicit close-point that differs from tank `MaxLevel`. (Users wanting a different close-point change the tank's `MaxLevel`.)
- Explicit "controlling tank" override (always `Node2` for v1).
- Float valves controlling on a remote node, on flow, or on pressure.
- Two-way (asymmetric rising/falling) valve curves.
- Inherent check-valve behaviour (bidirectional flow is allowed for v1; users wanting one-directional behaviour put a CV pipe in series).
- GUI / EPANET-GUI changes.

---

## 3. INP File Format

### 3.1 Grammar

```
flv_line ::= ID Node1 Node2 Diameter "FLV" Setting MinorLoss [ Curve ]
```

Identical to PCV's grammar. Token positions:

| Pos | Token     | Required | Notes                                                                      |
| --- | --------- | -------- | -------------------------------------------------------------------------- |
| 1   | ID        | yes      | Standard link ID.                                                          |
| 2   | Node1     | yes      | Upstream node (any node type).                                             |
| 3   | Node2     | yes      | **Must be a `[TANKS]` node.** Reservoirs and junctions are rejected.       |
| 4   | Diameter  | yes      | Valve diameter, length units of the project.                               |
| 5   | Type      | yes      | Literal `FLV`.                                                             |
| 6   | Setting   | yes      | **Regulating range** — depth of the modulating band below tank `MaxLevel`. |
| 7   | MinorLoss | yes      | Fully-open minor loss coefficient (`K_open`).                              |
| 8   | Curve     | no       | Valve characteristic curve ID; defaults to linear if omitted.              |

### 3.2 Examples

```
[VALVES]
;ID    Node1  Node2  Diameter  Type  Setting  MinorLoss  Curve
FV1    J1     T1     150       FLV   0.4      10
FV2    J2     T2     100       FLV   0.5      8
FV3    J3     T3     150       FLV   0.4      10         VC1

[CURVES]
;ID    pctOpen  pctFlow
VC1    0        0
VC1    25       10
VC1    50       40
VC1    75       75
VC1    100      100
```

### 3.3 Validation

Errors are fatal; warnings are reported but allow the run to proceed.

| Condition                                 | Severity | New error needed |
| ----------------------------------------- | -------- | ---------------- |
| Node2 is not a tank                       | error    | reuse existing   |
| Setting <= 0                              | error    | reuse            |
| Setting > (tank.MaxLevel − tank.MinLevel) | error    | reuse            |
| MinorLoss < 0                             | error    | reuse            |
| Curve ID does not exist in `[CURVES]`     | error    | reuse            |

The Setting validity check applies wherever Setting is set: at INP parse, in `[CONTROLS]` / `[RULES]` parse, and in `EN_setlinkvalue(EN_SETTING / EN_INITSETTING)` calls. Reuse PCV's existing setting-range validation path; it already runs at all three of these points for `[0, 100]` and the FLV check substitutes `(0, MaxLevel − MinLevel]`.

---

## 4. Engine Semantics

### 4.1 Definitions

For an FLV `v` with controlling tank `T` (= `Node2`):

```
top         = T.MaxLevel                         (top water level, height above tank bottom)
bot         = top - v.Setting                    (Setting carries the regulating range)
H_T         = current tank head
elev_T      = tank bottom elevation
depth_T     = H_T - elev_T                       (current depth above tank bottom)
K_open      = v.MinorLoss
f(p)        = curve value at percent-open p, or p/100 if no curve
```

### 4.2 Status

FLV uses the existing EPANET `StatusType` field (`hyd->LinkStatus[i]`) and follows the same convention as PRV / PSV / FCV:

- **`ACTIVE`** — the valve is regulating. For FLV, this is the normal state. `LinkSetting[i]` holds the regulating range.
- **`OPEN`** — fully open. For FLV, set by user override (`[STATUS] OPEN` / API / control / rule). When status is `OPEN` due to user override, `LinkSetting[i] == MISSING` (matches the existing convention for valves above PUMP, see `setlinkstatus()` in `hydraul.c`).
- **`CLOSED`** — fully shut. Set by user override the same way; `LinkSetting[i] == MISSING`.

The user override "lock" is exactly the existing mechanism: `Stat == OPEN/CLOSED` together with `Setting == MISSING`. There is no auxiliary flag. This is the same way PRV / PSV / FCV represent a forced-open or forced-closed state.

Status transitions follow the same paths the existing engine uses for FCV:

- `setlinkstatus(OPEN/CLOSED)` for an FLV (which sits in the `t > PUMP && t != GPV` branch already in `setlinkstatus()`): writes `Setting = MISSING`, `Stat = OPEN/CLOSED`. No FLV-specific code needed here — the existing branch already does this for any control valve.
- `setlinksetting(value)` for an FLV: behaves like FCV — writes `Setting = value`, `Stat = ACTIVE`. Needs a one-line addition in `setlinksetting()` to put FLV alongside FCV in that branch (or a small refactor; see §8.1).
- `[STATUS] ACTIVE` for an FLV at parse time: `InitStatus = ACTIVE`. `inithyd()` already promotes valves to ACTIVE status on simulation start when their setting is non-MISSING.

### 4.3 Per-step computation

A new step-level update is run once per hydraulic timestep, after `controls()` and before `hydsolve()` in `runhyd()`. It walks every FLV and updates `Stat`, `Setting`, and the link's loss-coefficient field (`link->R`, the same field PCV uses) based on the controlling tank's current depth:

```
for each FLV v:
    if v.Stat == OPEN  and v.Setting == MISSING:  continue   # user-forced open
    if v.Stat == CLOSED and v.Setting == MISSING: continue   # user-forced closed

    # v is ACTIVE with Setting = regulating range
    if depth_T >= top:
        v.Stat = CLOSED                # tank full — natural closure
    elif depth_T <= bot:
        v.Stat = ACTIVE                # fully open within band; R reflects K_open
        v.R    = K_open
    else:
        v.Stat = ACTIVE
        pctOpen = (top - depth_T) / v.Setting * 100
        v.R    = K_open / f(pctOpen)^2
```

After this update, `hydsolve()` runs with `Stat` and `R` already set. Within the iteration, the FLV is treated by the existing valve-coefficient code the same way a PCV is — its `R` is just used directly. No FLV-specific code in the iteration path.

The natural closure case (`depth_T >= top`) sets `Stat = CLOSED` for the duration of the step, but **does not** clear the Setting. On the next step, if the tank has dropped below `top`, this same logic restores `Stat = ACTIVE`. This matches how PRV's `Stat` flips between OPEN/ACTIVE/CLOSED automatically without losing the configured setting.

The engine does not shorten timesteps to track FLV state changes. The valve setting lags actual tank level by up to one hydraulic timestep; this is intentional and equivalent to how patterns and demands are sampled.

### 4.4 Settings, Status, Controls, Rules, and the API

Once §4.2 is in place, FLV inherits PCV's behaviour everywhere setting and status are touched:

- `[STATUS]` accepts `OPEN`, `CLOSED`, and `ACTIVE`. Already handled by the existing parser.
- `[CONTROLS]` may set FLV setting (a range value) or status (OPEN / CLOSED / ACTIVE). Already handled by `controls()`. The Setting validity check (`(0, MaxLevel − MinLevel]`) needs to apply when control values are validated.
- `[RULES]` THEN/ELSE actions may set FLV setting or status. Already handled by `rules.c`.
- `EN_setlinkvalue(EN_SETTING, x)` sets the regulating range, validated against `(0, MaxLevel − MinLevel]`.
- `EN_setlinkvalue(EN_STATUS, EN_OPEN / EN_CLOSED / EN_ACTIVE)` works via `setlinkstatus()` and `setlinksetting()`.
- `EN_setlinkvalue(EN_VALVE_CURVE, ...)` and `EN_setlinkvalue(EN_MINORLOSS, ...)` work as for PCV.

Mutations made between steps take effect at the start of the next hydraulic step (i.e. the next call to `runhyd()`), because the FLV update in §4.3 reads the current Setting and Stat each step.

A useful illustrative pattern — varying the regulating range by time of day:

```
[CONTROLS]
LINK FV1 0.6 AT TIME 06:00
LINK FV1 0.3 AT TIME 22:00
```

Or based on another tank's level (rules):

```
[RULES]
RULE narrow_band_when_t2_high
IF NODE T2 LEVEL > 4.5
THEN LINK FV1 SETTING IS 0.3
ELSE LINK FV1 SETTING IS 0.6
```

---

## 5. API

### 5.1 New constants

In `toolkit.h`:

```c
typedef enum {
    ...
    EN_FLV = 11               // float valve (LinkType)
} EN_LinkType;
```

`EN_FLV` should follow the next free slot in the existing enum; the value above is illustrative.

There are no new `EN_LinkProperty` constants. There are no new status constants. FLV uses the same property and status accessors as PCV.

### 5.2 Property accessors

All FLV properties are read and written through existing PCV accessors. All work pre-sim and mid-sim, identical to PCV.

| Function          | Property         | Meaning for FLV                                      |
| ----------------- | ---------------- | ---------------------------------------------------- |
| `EN_getlinktype`  | —                | Returns `EN_FLV`.                                    |
| `EN_getlinkvalue` | `EN_INITSETTING` | Initial regulating range.                            |
| `EN_setlinkvalue` | `EN_INITSETTING` | Initial regulating range. Validated.                 |
| `EN_getlinkvalue` | `EN_SETTING`     | Current regulating range.                            |
| `EN_setlinkvalue` | `EN_SETTING`     | Regulating range. Validated. Effective at next step. |
| `EN_getlinkvalue` | `EN_MINORLOSS`   | `K_open`.                                            |
| `EN_setlinkvalue` | `EN_MINORLOSS`   | `K_open`. Effective at next step.                    |
| `EN_getlinkvalue` | `EN_VALVE_CURVE` | Curve index, 0 if none.                              |
| `EN_setlinkvalue` | `EN_VALVE_CURVE` | Set/clear valve curve. Effective at next step.       |
| `EN_getlinkvalue` | `EN_STATUS`      | Status code (same as PCV).                           |
| `EN_setlinkvalue` | `EN_STATUS`      | Set OPEN / CLOSED / ACTIVE. Effective at next step.  |

### 5.3 Setting-value validation

`EN_SETTING` and `EN_INITSETTING` writes for FLV are validated with the same dispatch already used for PCV's `[0, 100]` check. For FLV the valid range is `(0, MaxLevel − MinLevel]` where the tank is `Node2`.

Out-of-range writes return a non-zero error code and leave Setting unchanged. Reuse the existing "invalid setting" error code that PCV uses.

---

## 6. INP Round-Trip

`EN_saveinpfile` writes FLV lines with the minimum number of trailing tokens required to preserve current state (same rule as PCV):

| State             | Tokens written                 |
| ----------------- | ------------------------------ |
| No curve assigned | 7 tokens (through `MinorLoss`) |
| Curve assigned    | 8 tokens (through `Curve`)     |

Round-trip invariant: parsing an INP, saving with `EN_saveinpfile`, and parsing again must yield identical FLV structures (same Setting, MinorLoss, Curve, Diameter, Node1, Node2, plus any `[STATUS]` and `[CONTROLS]` / `[RULES]` referencing the FLV).

---

## 7. Error Codes

No new error codes are required. The Node2-not-a-tank check, setting-out-of-range check, missing-curve check, and "negative/non-positive" check all reuse existing codes. The runtime-mutation lockout is no longer needed.

---

## 8. Implementation Plan

### 8.1 File touch list (OWA-EPANET 2.3 dev branch)

| File         | Change                                                                                                                                                                                                                                                |
| ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `types.h`    | Add `FLV` to `LinkType` enum.                                                                                                                                                                                                                         |
| `text.h`     | Add `w_FLV` keyword string.                                                                                                                                                                                                                           |
| `enumstxt.h` | Add `"FLV"` to `LinkTxt[]` (or equivalent).                                                                                                                                                                                                           |
| `toolkit.h`  | Add `EN_FLV`.                                                                                                                                                                                                                                         |
| `input2.c`   | In `getlinkdata` / `findvalve`, route `FLV` keyword to the existing PCV-shaped parsing branch.                                                                                                                                                        |
| `input3.c`   | In `valvedata()`, accept FLV using the PCV token layout. Validate Node2 is a tank.                                                                                                                                                                    |
| `inpfile.c`  | In `savevalves` (or equivalent), emit FLV with the same 7/8-token shape as PCV.                                                                                                                                                                       |
| `hydraul.c`  | In `setlinksetting()`, add `FLV` to the FCV branch so a numerical setting also forces `Stat = ACTIVE` (FLV is an active control valve, like FCV). The existing `setlinkstatus()` already handles FLV correctly via its `t > PUMP && t != GPV` branch. |
| `hydraul.c`  | Add a new function `flvstatus(Project *)` that walks every FLV and updates `Stat` and `link->R` per §4.3. Call it from `runhyd()` after `controls()` and before `hydsolve()`.                                                                         |
| `epanet.c`   | Extend the existing setting-validation branch in `EN_setlinkvalue(EN_SETTING / EN_INITSETTING)` to apply the FLV validity range `(0, MaxLevel − MinLevel]` when the link type is FLV.                                                                 |

`controls.c`, `rules.c`, `timestep.c`, `error.h`, `errors.dat` are **not** modified. FLV slots into the existing PCV/FCV machinery for these paths.

### 8.2 Data structure changes

No changes to `Svalve`. The existing `Setting` field stores the regulating range for FLV. The only struct-level change in the codebase is the addition of `FLV` to the `LinkType` enum.

### 8.3 Suggested implementation order

1. Add `FLV` to `LinkType`, keyword strings, and `LinkTxt`. Engine still rejects FLV at parse stage.
2. Parser branch in `valvedata()` + Node2 tank check. Round-trip parse without running.
3. `inpfile.c` writer emits FLV exactly like PCV. Round-trip parse → save → parse.
4. Add `flvstatus()` to `hydraul.c` and call it from `runhyd()` after `controls()`. Compute `Stat` and `link->R` per §4.3. Run §9.3 hydraulic tests.
5. Add FLV to the FCV branch of `setlinksetting()` so a numerical setting forces `Stat = ACTIVE`. Confirm `setlinkstatus()` already handles FLV correctly via its existing `t > PUMP && t != GPV` branch.
6. Setting-value validation in `EN_setlinkvalue` for FLV. Run §9.4 API tests.
7. End-to-end pass: `[STATUS]`, `[CONTROLS]`, `[RULES]`, mid-sim API. Run §9.5 / §9.6 / §9.7.
8. Final round-trip sweep.

---

## 9. Test Plan

All tests are written against the C toolkit (style: existing OWA-EPANET test suite using Boost.Test, or equivalent for the harness in use). Numerical tolerances are absolute unless stated.

### 9.1 Reference network

A fixed reference network used by most hydraulic tests. Save as `flv_ref.inp`:

```
[TITLE]
FLV reference network

[JUNCTIONS]
;ID    Elev   Demand
J1     100    0

[RESERVOIRS]
;ID    Head
R1     150

[TANKS]
;ID    Elev   InitLvl  MinLvl  MaxLvl  Diam   Volume
T1     100    4.0      0.0     5.0     11.28  0

[PIPES]
;ID    Node1  Node2  Length  Diam   Roughness  Mloss  Status
P1     R1     J1     1000    300    130        0      OPEN

[VALVES]
;ID    Node1  Node2  Diameter  Type  Setting  MinorLoss
FV1    J1     T1     200       FLV   0.5      5

[OPTIONS]
Units              LPS
Headloss           H-W
Quality            None

[TIMES]
Duration           24:00
Hydraulic Timestep 0:05
Pattern Timestep   1:00

[REPORT]
Status             Full

[END]
```

Tank `T1` has `MaxLevel = 5.0`, so `top = 5.0`. With `Setting = 0.5`, `bot = 4.5`. Tank cross-sectional area is approximately 100 m² (diameter 11.28 m). Tank operating range = `MaxLevel − MinLevel = 5.0`.

### 9.2 Parser tests

| Test ID | Description                                                                 | Expected                                                                          |
| ------- | --------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| P-01    | Parse a minimal 7-token FLV.                                                | `EN_getlinktype` returns `EN_FLV`. Curve = 0.                                     |
| P-02    | Parse 8-token FLV with a curve.                                             | `EN_VALVE_CURVE` returns the correct curve index.                                 |
| P-03    | Parse FLV where Node2 is a junction.                                        | Parser returns existing "node is not a tank" error.                               |
| P-04    | Parse FLV where Node2 is a reservoir.                                       | Parser returns existing "node is not a tank" error.                               |
| P-05    | Parse FLV with negative Setting.                                            | Parser returns error.                                                             |
| P-06    | Parse FLV with `Setting == 0`.                                              | Parser returns error.                                                             |
| P-07    | Parse FLV with `Setting > (tank.MaxLevel - tank.MinLevel)`.                 | Parser returns error.                                                             |
| P-08    | Parse FLV with `Setting == (tank.MaxLevel - tank.MinLevel)` exactly.        | Parse succeeds.                                                                   |
| P-09    | Parse FLV with `Curve` referencing a missing curve ID.                      | Parser returns existing missing-curve error.                                      |
| P-10    | Parse FLV with `[STATUS] FV1 OPEN`, `CLOSED`, or `ACTIVE`.                  | Parse succeeds for all three.                                                     |
| P-11    | Parse `[CONTROLS] LINK FV1 0.6 AT TIME 6:00`.                               | Parse succeeds; control accepted.                                                 |
| P-12    | Parse `[CONTROLS] LINK FV1 -0.1 AT TIME 6:00` (invalid setting in control). | Parser returns error (matches PCV's behaviour for out-of-range control settings). |
| P-13    | Parse `[CONTROLS] LINK FV1 OPEN IF NODE T1 BELOW 1.0`.                      | Parse succeeds.                                                                   |
| P-14    | Parse `[RULES]` block targeting FLV setting / status.                       | Parse succeeds.                                                                   |

### 9.3 Hydraulic tests

`top = 5.0`, `bot = 4.5`, `K_open = 5`.

| Test ID | Setup                                  | Expected                                                                                |
| ------- | -------------------------------------- | --------------------------------------------------------------------------------------- |
| H-01    | InitLvl = 5.0 (depth at top)           | Step 0 status = Closed. Flow ≈ 0 (within solver tolerance).                             |
| H-02    | InitLvl = 4.0 (depth ≤ bot)            | Step 0 status = Open. Flow > 0. K_active == K_open.                                     |
| H-03    | InitLvl = 4.75 (depth in band)         | Step 0 status = Active. pctOpen = (5.0 − 4.75)/0.5 × 100 = 50.0 ± 1e-6.                 |
| H-04    | InitLvl = 4.75, no curve               | K_active = 5 / 0.5² = 20.0 ± 1e-6.                                                      |
| H-05    | InitLvl = 4.75, curve = VC1 (50 → 40)  | K_active = 5 / 0.40² = 31.25 ± 1e-6.                                                    |
| H-06    | InitLvl = 4.5 (depth at bot, boundary) | pctOpen = 100. Status = Open (boundary inclusive on the open side).                     |
| H-07    | InitLvl = 5.0 (depth at top, boundary) | Status = Closed (boundary inclusive on the closed side).                                |
| H-08    | InitLvl = 4.0, run for 6 hours         | Tank fills monotonically toward TWL. pctOpen decreases as depth rises through the band. |
| H-09    | InitLvl = 4.0, `[STATUS] FV1 OPEN`     | Valve held Open for the whole run. K = K_open at every step.                            |
| H-10    | InitLvl = 4.0, `[STATUS] FV1 CLOSED`   | Valve held Closed for the whole run. No flow into tank. pctOpen never computed.         |
| H-11    | InitLvl = 4.0, `[STATUS] FV1 ACTIVE`   | Identical to H-08 (ACTIVE is the default; explicit declaration is a no-op).             |

### 9.4 API setting / validation tests

| Test ID | Sequence                                                              | Expected                                  |
| ------- | --------------------------------------------------------------------- | ----------------------------------------- |
| A-01    | Pre-sim: `EN_setlinkvalue(EN_SETTING, 0.6)` on FLV.                   | Returns 0. `EN_getlinkvalue` returns 0.6. |
| A-02    | Pre-sim: `EN_setlinkvalue(EN_SETTING, -0.1)`.                         | Returns non-zero error code.              |
| A-03    | Pre-sim: `EN_setlinkvalue(EN_SETTING, 0.0)`.                          | Returns non-zero error code.              |
| A-04    | Pre-sim: `EN_setlinkvalue(EN_SETTING, 5.0)` (= MaxLevel − MinLevel).  | Returns 0 (boundary inclusive).           |
| A-05    | Pre-sim: `EN_setlinkvalue(EN_SETTING, 5.1)` (> MaxLevel − MinLevel).  | Returns non-zero error code.              |
| A-06    | Pre-sim: `EN_setlinkvalue(EN_VALVE_CURVE, c)` with valid curve index. | Returns 0. Curve assigned.                |

### 9.5 Mid-simulation API tests

| Test ID | Sequence                                                                       | Expected                                                                                    |
| ------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------- |
| M-01    | Run a few steps; mid-sim `EN_setlinkvalue(EN_SETTING, 0.3)`. Continue running. | New range takes effect at the next step. `bot` becomes 4.7. pctOpen recomputed accordingly. |
| M-02    | Mid-sim `EN_setlinkvalue(EN_SETTING, 6.0)` (out of range).                     | Returns non-zero error. Setting unchanged.                                                  |
| M-03    | Mid-sim `EN_setlinkvalue(EN_STATUS, EN_OPEN)`.                                 | Next step: status = Open, K = K_open regardless of tank level.                              |
| M-04    | Mid-sim `EN_setlinkvalue(EN_STATUS, EN_CLOSED)`.                               | Next step: status = Closed, no flow.                                                        |
| M-05    | Following M-03: mid-sim `EN_setlinkvalue(EN_STATUS, EN_ACTIVE)`.               | Next step: status returns to float-controlled behaviour.                                    |
| M-06    | Mid-sim `EN_setlinkvalue(EN_VALVE_CURVE, c)`.                                  | Next step: K_active uses new curve.                                                         |
| M-07    | Mid-sim `EN_setlinkvalue(EN_MINORLOSS, x)`.                                    | Next step: K_open updated; K_active recomputed.                                             |
| M-08    | Identical mid-sim sequences against a PCV link.                                | PCV behaviour unchanged. Confirms FLV uses the existing path without regression.            |

### 9.6 `[CONTROLS]` tests

| Test ID | Setup                                                         | Expected                                                    |
| ------- | ------------------------------------------------------------- | ----------------------------------------------------------- |
| C-01    | `[CONTROLS] LINK FV1 0.3 AT TIME 6:00`. Run past 6:00.        | Setting is 0.5 before 06:00, becomes 0.3 from 06:00 onward. |
| C-02    | Two timed setting controls (e.g. 0.6 at 06:00, 0.3 at 22:00). | Setting transitions on schedule.                            |
| C-03    | `[CONTROLS] LINK FV1 OPEN IF NODE T1 BELOW 1.0`.              | When tank depth < 1.0, valve is forced Open.                |
| C-04    | `[CONTROLS] LINK FV1 CLOSED IF NODE J1 ABOVE 200`.            | When J1 head > 200, valve is forced Closed.                 |
| C-05    | `[CONTROLS] LINK FV1 ACTIVE IF NODE T1 ABOVE 2.0`.            | When tank depth > 2.0, valve returns to regulating.         |

### 9.7 `[RULES]` tests

| Test ID | Setup                                                                                               | Expected                                                                                                            |
| ------- | --------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| RU-01   | Rule sets `LINK FV1 SETTING IS 0.3` when another tank's level exceeds a threshold; `0.6` otherwise. | FV1's regulating range tracks the rule's IF/ELSE branches across the run.                                           |
| RU-02   | Rule sets `LINK FV1 STATUS IS CLOSED` IF some condition.                                            | Valve forced Closed while condition holds; resumes regulating when condition clears (with explicit ACTIVE in ELSE). |

### 9.8 Round-trip tests

| Test ID | Description                                                        | Expected                                                                             |
| ------- | ------------------------------------------------------------------ | ------------------------------------------------------------------------------------ |
| R-01    | Parse `flv_ref.inp`, `EN_saveinpfile` to new file, parse new file. | Both networks have identical FLV: Setting, MinorLoss, Curve, Diameter, Node1, Node2. |
| R-02    | No-curve case (7-token input).                                     | Saved file emits 7 tokens (no Curve).                                                |
| R-03    | With curve (8-token input).                                        | Saved file emits 8 tokens.                                                           |
| R-04    | `[CONTROLS]` and `[RULES]` referencing FV1.                        | Saved file preserves them verbatim.                                                  |
| R-05    | `[STATUS] FV1 OPEN` in input, save.                                | Saved file's `[STATUS]` section contains `FV1 OPEN`.                                 |
| R-06    | After mid-sim `EN_setlinkvalue(EN_SETTING, x)`, save and reparse.  | New Setting persisted in saved file.                                                 |

### 9.9 Numerical reference values

Used to verify `K_active` and `pctOpen` formulas.

| K_open | pctOpen | curve    | f(p) | K_active expected |
| ------ | ------- | -------- | ---- | ----------------- |
| 10     | 100     | linear   | 1.0  | 10.0              |
| 10     | 50      | linear   | 0.50 | 40.0              |
| 10     | 25      | linear   | 0.25 | 160.0             |
| 10     | 10      | linear   | 0.10 | 1000.0            |
| 5      | 50      | VC1 (40) | 0.40 | 31.25             |
| 5      | 75      | VC1 (75) | 0.75 | 8.888...          |

Tolerance: relative 1e-9 on `K_active` for these unit checks.

For `pctOpen` from depth (top=5.0, Setting=0.5):

| depth | pctOpen expected |
| ----- | ---------------- |
| 4.0   | (clamped) Open   |
| 4.5   | 100.0            |
| 4.6   | 80.0             |
| 4.75  | 50.0             |
| 4.9   | 20.0             |
| 5.0   | (clamped) Closed |

Tolerance: 1e-9.

---

## 10. Worked Examples

### Example 1: Modulating float valve, no custom curve

Tank `T1` has `MaxLevel = 5.0`.

```
[VALVES]
FV1  J1  T1  200  FLV  0.5  5
```

At depth 4.75 m: pctOpen = 50%, K_active = 20.

### Example 2: Cla-Val style float valve, calibrated curve

Tank `T1` has `MaxLevel = 2.175`.

```
[CURVES]
;ID    pctOpen  pctFlow
CLA50  0        0
CLA50  20       5
CLA50  40       18
CLA50  60       45
CLA50  80       78
CLA50  100      100

[VALVES]
FV1  J1  T1  150  FLV  0.4  10  CLA50
```

### Example 3: Time-varying regulating range

Tighten the modulating band overnight (smaller range → sharper response):

```
[VALVES]
FV1  J1  T1  150  FLV  0.6  5

[CONTROLS]
LINK FV1 0.3 AT TIME 22:00
LINK FV1 0.6 AT TIME 06:00
```

### Example 4: Range varied by another tank's level (rule)

Narrow FV1's band when feeder tank T2 is high (push more water through quickly):

```
[VALVES]
FV1  J1  T1  150  FLV  0.6  5

[RULES]
RULE narrow_when_t2_high
IF NODE T2 LEVEL > 4.5
THEN LINK FV1 SETTING IS 0.3
ELSE LINK FV1 SETTING IS 0.6
PRIORITY 1
```

### Example 5: Scenario-disabled float valve (forced open)

```
[VALVES]
FV1  J1  T1  150  FLV  0.5  1.0

[STATUS]
FV1  OPEN
```

`[STATUS] OPEN` pins the valve fully open for the entire run, bypassing float behaviour. Useful for "what if the float valve fails open" scenarios. Use `CLOSED` for "what if the inlet is isolated".

---

## 11. Open items the implementer should flag, not decide

The following are intentionally not pinned down and should surface as questions during review rather than be invented:

- Exact integer value for `EN_FLV`. Use the next free slot in `EN_LinkType`; record what was chosen.
- Exact existing error code to reuse for "Node2 is not a tank". Locate and cite it.
- Exact existing error code that PCV uses for setting-out-of-range; FLV reuses the same code.
- Precise wording of warning/error message strings (none new, but FLV's setting-range error message should mention the tank operating range so users can debug).
- Whether the FLV's reported flow should be zero or negligible-but-nonzero when Closed (match whatever PCV does today).
- Behaviour when a `[STATUS] OPEN`-locked FLV would otherwise overfill the tank past `MaxLevel`. Match whatever EPANET already does for similar tank-overfill scenarios with an active feed.
