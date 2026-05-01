# Float Valve Inlet Height (FLV InletHeight) — EPANET Implementation Specification

## 1. Overview

This specification extends the existing **FLV** (Float Valve) link type with an optional **`InletHeight`** field that lets users place the valve's discharge point partway up the controlling tank wall — including above the tank's top water level. It is a delta against `flv-specification.md`; everything in that document still applies, except where this document overrides it.

The motivation: in real water-supply networks, tank inlets are rarely at the tank floor. They are commonly placed near the top of the tank (so the inlet pipe stays full and the float arm can act on it), or the inlet may discharge in free fall over the tank rim onto the water surface. When the inlet sits above the current water surface, the valve's downstream boundary is no longer the tank head — it is the elevation of the pipe outlet at atmospheric pressure, and flow is naturally one-way (the column of water in the pipe cannot draw water back out of the tank by siphon, because the pipe is broken to atmosphere at the inlet).

The single new INP field — `InletHeight` — captures the elevation of the pipe outlet above the tank floor (`T.Elev`). It uses the same reference frame EPANET already uses for `MinLevel`, `MaxLevel`, and `InitLevel`, so a user reading any of those fields can compare `InletHeight` to them directly. Default `0` (current FLV behaviour: inlet at tank floor, downstream head = tank head, bidirectional flow allowed).

In addition to changing the *downstream boundary head*, the inlet height also changes the **reference for the regulating band**. Today, FLV's modulating band sits immediately below the top water level (TWL = `MaxLevel`): the float is assumed to be at TWL, the valve closes when the water reaches TWL, and the band extends `Setting` metres below it. With this extension, the float is assumed to sit at the inlet pipe — so when `0 < InletHeight <= MaxLevel`, the band sits immediately below `InletHeight` instead. When `InletHeight = 0` (legacy) or `InletHeight > MaxLevel` (inlet over the rim, water can never reach it), the band stays at TWL, exactly as today. So a 1 m regulating range with `InletHeight = 5 m` and `MaxLevel = 7.5 m` regulates between depths 4 m and 5 m — *not* 6.5 m and 7.5 m. This matches how a real float valve behaves: the float closes around its own physical position, not the top of the tank.

### 1.1 Mode summary

For an FLV with controlling tank `T` and `inlet_elev = T.Elev + InletHeight`:

| Tank water surface vs. `inlet_elev`     | Mode      | Downstream head used by valve | Flow direction       |
| --------------------------------------- | --------- | ----------------------------- | -------------------- |
| `H_T >= inlet_elev` (inlet submerged)   | SUBMERGED | `H_T` (existing FLV)          | bidirectional        |
| `H_T <  inlet_elev` (inlet above water) | CASCADE   | `inlet_elev`                  | one-way (J1 → tank)  |

Mode is decided once per hydraulic timestep, in `flvstatus()`, alongside the existing FLV pctOpen/loss-coefficient calculation — which is itself computed from the new inlet-relative regulating band. The cascade-mode head boundary is enforced in `linkcoeffs()` by substituting `inlet_elev` for the tank's stored head on the single line that injects the tank-as-known-head boundary into the matrix RHS. The one-way constraint is enforced exactly the way EPANET enforces CV pipes: status flips to `CLOSED` if the iteration produces reverse flow.

The water that flows in during cascade mode still adds to the tank's volume — the pipe simply discharges to atmosphere; gravity does the rest. No tank-volume accounting changes are needed.

### 1.2 Composition with the FLV regulating band

Two mechanisms share `flvstatus()`:

1. **Regulating band reference** (changed by this spec). The band's top reference (`top`) used to be `MaxLevel` unconditionally; it now becomes `InletHeight` when `0 < InletHeight <= MaxLevel`, and stays at `MaxLevel` otherwise. `bot = top - Setting`. pctOpen and `link->R` are derived from the tank's water-surface position within `[bot, top]` exactly as before — only the values of `top` and `bot` shift.

2. **Downstream boundary head** (introduced by this spec). When the water surface is below `inlet_elev`, the matrix sees `inlet_elev` as the downstream head; when at or above, it sees the tank head.

The two mechanisms are evaluated from the same `H_T` snapshot at step start. With the new band reference, an interesting consequence is that whenever the valve is *modulating* (i.e. ACTIVE with depth in the regulating band) and `0 < InletHeight <= MaxLevel`, the water surface is by construction below `InletHeight`, so the valve is in CASCADE mode while it modulates. In other words: for inlets within the tank, modulation always happens against a free-fall boundary, never against the tank's water column. Worked example in §10.

---

## 2. Goals and Non-Goals

### Goals

- Add an optional `InletHeight` token to the FLV INP grammar; default `0` keeps existing behaviour bit-identical.
- Change the regulating-band reference from `MaxLevel` to `min(InletHeight, MaxLevel)` when `InletHeight > 0`. With `InletHeight = 0`, the band stays at `MaxLevel` (legacy behaviour preserved).
- When the tank water surface is at or above `inlet_elev`, the matrix sees the tank's hydraulic head — identical to today's FLV in the cascade-irrelevant case.
- When the tank water surface is below `inlet_elev`, the valve's downstream-side hydraulic boundary is `inlet_elev` (atmospheric pressure at the pipe outlet), and reverse flow is prohibited.
- Mode is sampled once per hydraulic timestep, same cadence as the existing FLV pctOpen calculation. No dynamic mode-switching within a Newton iteration.
- INP round-trip preserves the `InletHeight` field. A non-default value is written; a default value (`0`) is omitted to keep diff size minimal on legacy inputs.
- INP-only feature — **no new API surface** in v1. (The implementer should not add `EN_setlinkvalue` / `EN_getlinkvalue` properties. We will revisit after the engine path is proven.)
- Smallest-possible diff to the C source. Reuse the existing tank-as-known-head primitive in `linkcoeffs()` and the existing CV-style status flip on reverse flow.
- Full coverage in `tests/test_valve.cpp` for the new INP grammar, the new regulating-band reference, both downstream-head modes, the mode-switch boundary, and round-trip.

### Non-Goals (deferred)

- API access to `InletHeight` (`EN_setlinkvalue` etc.). Out of scope for v1.
- Modelling cascade energy losses, splash, or air entrainment. Cascade is treated as a free-discharge boundary with no extra loss term beyond the valve's own minor loss.
- Per-step hysteresis on the SUBMERGED ↔ CASCADE switch. We rely on the timestep-level sampling: mode is decided once per step from `H_T` at step start and held for the step. (See §11 — open question on whether a small dead-band would be worth it.)
- Inherent reverse-flow allowance when submerged. We retain the existing FLV behaviour (bidirectional when submerged); CV semantics apply only in cascade mode.
- Multiple inlets on a single tank with different heights. Already supported trivially: each is its own FLV link; nothing in this spec interacts.
- GUI / EPANET-GUI changes.
- Two-way (asymmetric rising/falling) cascade behaviour.

---

## 3. INP File Format

### 3.1 Grammar

```
flv_line ::= ID Node1 Node2 Diameter "FLV" Setting MinorLoss [ InletHeight [ Curve ] ]
```

Token positions:

| Pos | Token          | Required | Notes                                                                                                  |
| --- | -------------- | -------- | ------------------------------------------------------------------------------------------------------ |
| 1-7 | ID … MinorLoss | yes      | Identical to existing FLV grammar.                                                                     |
| 8   | InletHeight    | optional | Elevation of the pipe outlet above the tank floor (`T.Elev`), project length units. Defaults to `0`.   |
| 9   | Curve          | optional | Curve ID. Defaults to linear if omitted. Cannot be supplied without token 8.                           |

**Note:** This is a deliberate reorder relative to the existing FLV grammar (which placed Curve at slot 8). Token 8 was previously Curve; it is now InletHeight. Legacy 7-token FLV lines (no curve, no InletHeight) parse identically. Legacy 8-token FLV lines that supplied a curve in slot 8 — created in the brief window since FLV shipped — must be migrated to the new 9-token form. Because FLV is brand-new (commit `c5a89f0`, not yet released), the migration cost is effectively the test fixtures shipped in that commit.

### 3.2 Examples

```
[VALVES]
;ID    Node1  Node2  Diameter  Type  Setting  MinorLoss  InletHeight  Curve
FV1    J1     T1     150       FLV   0.4      10                                  ; legacy: inlet at floor, no curve
FV2    J2     T2     100       FLV   0.5      8         2.0                       ; inlet 2 m above tank floor, no curve
FV3    J3     T3     150       FLV   0.4      10        4.5          VC1          ; inlet near TWL, with curve
FV4    J4     T4     150       FLV   0.4      10        6.0          VC1          ; inlet above MaxLevel (always cascade)
```

### 3.3 Validation

Errors are fatal; warnings allow the run to proceed.

| Condition                                                                      | Severity | Error code                                                                            |
| ------------------------------------------------------------------------------ | -------- | ------------------------------------------------------------------------------------- |
| `InletHeight < 0`                                                              | error    | `202` ("illegal numeric value") — same code FLV's existing `Setting` check returns    |
| `InletHeight > T.MaxLevel + headroom` (if any)                                 | warning  | new warning, or reuse generic warning slot                                            |
| Token 9 supplied with non-tank Node2                                           | error    | already covered by FLV's tank check                                                   |
| `Setting <= 0` or `Setting > reg_height − MinLevel` (band would extend below `MinLevel`) | error | `202`, returned the same way the existing `Setting <= 0 \|\| Setting > Hmax − Hmin` check does |

`InletHeight > T.MaxLevel` is **not** an error. It models an inlet that always discharges in cascade — this is a legitimate configuration (e.g. inlet at the tank rim with a free-fall drop). A warning is optional and recommended only if the value is implausibly large (e.g. `> 2 * MaxLevel`); see §11.

The existing FLV check lives at [`src/input3.c:673-679`](src/input3.c#L673-L679):

```c
if (type == FLV)
{
    Stank *t = &net->Tank[j2 - net->Njuncs];
    double range = t->Hmax - t->Hmin;
    if (link->Kc <= 0.0 || link->Kc > range)
        return setError(parser, 5, 202);
}
```

This generalises by changing `range` to use `reg_height − MinLevel` instead of `Hmax − Hmin`. Concretely, after `link->InletHeight` has been parsed:

```c
if (type == FLV)
{
    Stank *t = &net->Tank[j2 - net->Njuncs];
    double maxlvl = t->Hmax - t->El;
    double reg_height = (link->InletHeight > 0.0 && link->InletHeight <= maxlvl)
                      ? link->InletHeight
                      : maxlvl;
    double range = reg_height - (t->Hmin - t->El);
    if (link->Kc <= 0.0 || link->Kc > range)
        return setError(parser, 5, 202);

    if (link->InletHeight < 0.0)
        return setError(parser, /* token index of InletHeight */, 202);
}
```

Same error code (`202`), same `setError` call shape, same fatal-at-parse semantics — just a tighter `range` for the inlet-relative band. No new error code is added.

`InletHeight` is validated at INP parse only. Because there is no API in v1, it cannot change at runtime; nothing else needs to revalidate.

---

## 4. Engine Semantics

### 4.1 Definitions (extending `flv-specification.md` §4.1)

```
inlet_height = v.InletHeight                       (0 if not set)
inlet_elev   = T.Elev + inlet_height               (absolute elevation of the pipe outlet)
H_T          = current tank head (= T.Elev + depth_T)

# Regulating-band reference (CHANGED by this spec)
reg_height   = T.MaxLevel                          if inlet_height == 0
               inlet_height                        if 0 < inlet_height <= T.MaxLevel
               T.MaxLevel                          if inlet_height >  T.MaxLevel
top          = T.Elev + reg_height                 (absolute elevation; replaces tank->Hmax in flvstatus())
bot          = top - v.Setting                     (Setting carries the regulating range, unchanged)

# Downstream-head mode (NEW by this spec)
mode         = SUBMERGED if H_T >= inlet_elev
               CASCADE   if H_T <  inlet_elev
```

`InletHeight` is measured from the tank floor (`T.Elev`), the same reference frame used by `MinLevel`, `MaxLevel`, and `InitLevel`. So `InletHeight = 0` means "at the tank floor" (preserves the existing FLV's behaviour exactly), and `InletHeight = MaxLevel` means "at the top water level".

The piecewise `reg_height` rule is the smallest expression that gives the user-requested behaviour at the three boundary cases:

- `inlet_height == 0` — legacy float-at-TWL behaviour. The float is conceptually at the top of the tank, so the band hugs `MaxLevel`.
- `0 < inlet_height <= MaxLevel` — float is at the inlet, inside the tank. Band hugs the inlet. Whenever the valve is modulating, water is below the inlet, so cascade mode is active.
- `inlet_height > MaxLevel` — inlet is over the tank rim, water can never reach it. Float can't be at the inlet, so we fall back to TWL. Cascade mode is always active when the valve is open.

### 4.2 Per-step computation (replaces `flv-specification.md` §4.3)

`flvstatus()` is extended in two ways:

- The `top` calculation now uses the `reg_height` rule from §4.1 instead of `tank->Hmax` unconditionally.
- After the existing pctOpen logic, mode is computed and stored on the link.

Pseudocode (changes from the existing implementation in `src/hydraul.c:644-697` are flagged with `# CHANGED` / `# NEW`):

```
for each FLV v with controlling tank T:
    # Mode is computed unconditionally, even for user-locked valves —
    # downstream boundary head depends on physical water level, not on
    # whether the float is engaged.
    if H_T >= inlet_elev:                                       # NEW
        v.Mode = SUBMERGED                                      # NEW
    else:                                                       # NEW
        v.Mode = CASCADE                                        # NEW

    if v.Stat == OPEN  and v.Setting == MISSING:  continue   # user-forced open
    if v.Stat == CLOSED and v.Setting == MISSING: continue   # user-forced closed

    # CHANGED: top now derives from inlet_height per §4.1, not tank->Hmax.
    reg_height = (v.InletHeight > 0 && v.InletHeight <= T.MaxLevel)
                 ? v.InletHeight
                 : T.MaxLevel
    top        = T.Elev + reg_height
    bot        = top - v.Setting

    if H_T >= top:
        v.Stat = CLOSED                # natural closure (do not clear setting)
        continue
    elif H_T <= bot:
        v.Stat = ACTIVE
        v.R    = K_open
    else:
        v.Stat = ACTIVE
        pctOpen = (top - H_T) / v.Setting * 100
        v.R    = K_open / f(pctOpen)^2
```

`v.Mode` is a per-link, per-step flag. Two choices for storage (see §11): a new field on `Slink` / `Svalve`, or repurpose an existing slot. Recommendation: a single `unsigned char` field on `Svalve`, set in `flvstatus()` and read in the matrix-assembly path.

**Why compute Mode unconditionally?** The downstream boundary head reflects physical reality (where the inlet pipe sits relative to the water surface), not the float's logic. A user-locked OPEN valve over a near-empty tank still discharges into free fall; a user-locked CLOSED valve still has its downstream boundary defined the same way (it's just zero-flow). Computing Mode regardless of `Setting == MISSING` keeps the matrix-assembly path correct in all cases.

### 4.3 Matrix assembly (single-line override)

Today, FLV uses `pcvcoeff(pr, k)` for its resistance, and the link's contribution to the matrix is then assembled by the standard junction/tank-aware loop in `linkcoeffs()` (`hydcoeffs.c:355-383`). The relevant lines are:

```c
// ... node n2 is a tank/reservoir
else sm->F[sm->Row[n1]] += (hyd->P[k] * hyd->NodeHead[n2]);   // hydcoeffs.c:382
```

i.e. the tank's head is already a fixed-head boundary; it enters the matrix only as a constant on the RHS. This is the existing primitive — the same mechanism that makes tanks act as known heads, and conceptually the same as how PRVs force a regulated head.

**Cascade mode is therefore a single-line substitution**: when the link being assembled is an FLV in cascade mode, use `inlet_elev` instead of `hyd->NodeHead[n2]` on that line. SUBMERGED mode and all non-FLV links use the existing path unchanged.

Sketch:

```c
double h_downstream = hyd->NodeHead[n2];
if (link->Type == FLV && link->Mode == CASCADE)
    h_downstream = pr->network.Node[n2].El + link->InletHeight;
sm->F[sm->Row[n1]] += (hyd->P[k] * h_downstream);
```

The valve's own resistance (`link->R`) is consumed exactly as today via `pcvcoeff`; only the boundary head changes. No new coefficient function is needed.

**Why not the `prvcoeff` penalty pattern?** I had originally drafted that. After looking at `linkcoeffs()`, the tank-RHS shape is already what we need — the penalty pattern would do equivalent work via a more invasive route. Implementer should prefer the substitution unless they find a correctness issue.

### 4.4 One-way flow (CV semantics) in cascade mode

Reverse flow (tank → J1) cannot physically occur in cascade mode — the pipe is open to atmosphere at the tank-side outlet. In the matrix iteration, this is enforced exactly the way CV pipes already enforce it (`hydstatus.c:182-200`, `cvstatus()`):

- If, at the end of an iteration, the cascade-mode FLV has computed `Q < 0`, flip its status to `CLOSED` for the remainder of the iteration loop.
- On the next timestep, `flvstatus()` re-evaluates: if the tank is still in cascade mode and `H_J1` still cannot reach `inlet_elev`, the valve stays `CLOSED`; otherwise it returns to `ACTIVE`.

Implementer note: when `H_J1 <= inlet_elev` at step start, cascade flow is impossible and the valve will close on the first iteration anyway. A pre-check in `flvstatus()` that sets `LinkStatus = CLOSED` directly when `H_J1 < inlet_elev` is a valid optimisation but not required for correctness.

### 4.5 Tank volume update

In cascade mode the water that arrives at the inlet falls into the tank. The volume entering the tank per timestep equals the time-integral of `Q` over the step, exactly as in submerged mode. **No changes to the tank-update code path.** EPANET already updates tank levels from net inflow regardless of where the inflow physically discharges.

### 4.6 No interaction with `[CONTROLS]`, `[RULES]`, or `[STATUS]`

`InletHeight` is a static link property in v1. None of the runtime mutation paths can change it. Forced-open / forced-closed via `[STATUS]` etc. work exactly as in `flv-specification.md` §4.4 — they bypass `flvstatus()` entirely, including the mode logic.

---

## 5. API

**No new API surface in v1.**

`EN_setlinkvalue` / `EN_getlinkvalue` do not gain an `EN_INLETHEIGHT` (or similar) property. `InletHeight` is read at INP parse time, written at `EN_saveinpfile` time, and otherwise inert from the API perspective.

This is a deliberate scope cut to prove the engine path works first. A follow-on spec will add API access once the hydraulic path is validated and shipped.

---

## 6. INP Round-Trip

`EN_saveinpfile` writes FLV lines with the minimum number of trailing tokens required to preserve current state:

| State                                          | Tokens written |
| ---------------------------------------------- | -------------- |
| `InletHeight == 0`, no curve                   | 7              |
| `InletHeight != 0`, no curve                   | 8              |
| `InletHeight != 0`, with curve                 | 9              |
| `InletHeight == 0`, with curve                 | 9 (must emit explicit `0` for InletHeight to reach the curve slot) |

Round-trip invariant: parsing an INP, saving with `EN_saveinpfile`, and parsing again must yield identical FLV structures (same Setting, MinorLoss, Curve, Diameter, Node1, Node2, **InletHeight**, plus any `[STATUS]` / `[CONTROLS]` / `[RULES]` referencing the FLV).

---

## 7. Error Codes

No new error codes. `InletHeight < 0` reuses the existing "value out of range" code that PCV/FLV setting validation uses today.

---

## 8. Implementation Plan

### 8.1 File touch list (OWA-EPANET 2.3 dev branch, on top of the FLV implementation in commit `c5a89f0`)

| File                  | Change                                                                                                                                                                   |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `src/types.h`         | Add `InletHeight` field to `Svalve` (or `Slink` if simpler); add a `Mode` byte for SUBMERGED / CASCADE; add the two enum values.                                          |
| `src/input3.c`        | In `valvedata()`, parse the optional 8th token (InletHeight) before the curve. Convert via `Ucf[ELEV]` on load (matches how Setting is converted today; see commit `c5a89f0`). Generalise the existing FLV `Setting` range check at lines 673-679 to use `reg_height − MinLevel` as the upper bound; same `setError(parser, 5, 202)` call. Add `setError(parser, <inlet token>, 202)` for `InletHeight < 0`. See §3.3. |
| `src/input1.c`        | If a default-init pass exists for valve fields, default `InletHeight` to `0`.                                                                                            |
| `src/inpfile.c`       | In `savevalves`, extend the FLV writer to emit token 8 when `InletHeight != 0`, and token 9 (Curve) after it. Apply the inverse `Ucf[ELEV]` conversion. Emit explicit `0` in slot 8 when a curve is present and InletHeight is 0. |
| `src/hydraul.c`       | In `flvstatus()`: (a) replace `top = tank->Hmax` with the `reg_height` rule from §4.1; (b) compute and store `link->Mode` per §4.2.                                      |
| `src/hydcoeffs.c`     | In `linkcoeffs()` (around line 382), substitute `inlet_elev` for `NodeHead[n2]` when the link is an FLV in cascade mode. Single-line conditional — see §4.3.             |
| `src/hydstatus.c`     | Add CV-style reverse-flow check for cascade-mode FLV in the appropriate iteration-level status sweep; mirror `cvstatus()`.                                              |
| `src/output.c`        | If valve fields are written to the binary output, ensure `InletHeight` is preserved (likely no change — check.)                                                          |
| `src/project.c`       | If `valvecheck()` validates `InletHeight`-related invariants, add the relevant guards. Likely just a non-negative check.                                                |
| `tests/test_valve.cpp`| Add the §9 test cases.                                                                                                                                                  |
| `tests/data/flv_ref.inp` | Migrate any 8-token FLV lines from commit `c5a89f0` to the new 9-token form (Curve moves to slot 9). Add an inlet-height variant fixture.                              |

`controls.c`, `rules.c`, `epanet.c` (API surface), `errors.dat`, `error.h`, `text.h`, `enumstxt.h` are **not** modified.

### 8.2 Data structure changes

Two new fields, both small:

- `double InletHeight` — on `Slink` or `Svalve`. Stored in internal (feet) units after parse, like other lengths. Defaults to 0.
- `unsigned char Mode` (or `int`, matching neighbouring fields) — per-link, written in `flvstatus()`, read in matrix assembly. Optional: could be derived on the fly each iteration from `H_T` and `inlet_elev` instead of stored, at the cost of recomputing per iteration.

Implementer should choose between a stored `Mode` and an inline recomputation; recommendation is stored, set once per step in `flvstatus()`, to keep the matrix-assembly path branch-free of tank lookups.

### 8.3 Suggested implementation order

1. Parser + writer for the optional 8th/9th tokens. Round-trip parse / save / parse with `InletHeight = 0` and a non-zero value, with and without a curve. Migrate existing fixtures. Add `tests/data/flv_uk_ref.inp` to the test set. (No engine changes yet.)
2. Update `flvstatus()`'s `top` calculation to use `reg_height`. Run the existing FLV test suite; with `InletHeight = 0` everywhere, all tests must still pass bit-identically (`reg_height = MaxLevel` in that case). Add new tests for `InletHeight > 0` band-reference behaviour (§9.3 HI-10..12).
3. Extend `flvstatus()` to compute and store `Mode`. Add a debug-only print or a unit test that reads `Mode` after step 0.
4. Add the cascade-mode matrix-assembly path. Verify §9.3 hydraulic tests pass.
5. Add the cascade-mode CV-style reverse-flow check. Verify §9.4.
6. End-to-end run of `flv_uk_ref.inp` with the recipes in §10 Example 6. Hand-check tank level evolution against expectations.
7. Final round-trip and regression sweep against the existing FLV test suite.

---

## 9. Test Plan

All tests are written against the C toolkit using the existing Boost.Test pattern in `tests/test_valve.cpp`. Tolerances are absolute unless stated.

### 9.1 Reference networks

Two fixtures are used.

**Synthetic — `tests/data/flv_ref.inp`** (created in commit `c5a89f0`). Single FLV between a junction and a tank. Tank `T1` has `Elev = 100`, `MinLevel = 0`, `MaxLevel = 5`. So `inlet_elev = 100 + InletHeight`. Used for the targeted parser, hydraulic, cascade, and round-trip tests below.

**Real-world — `tests/data/flv_uk_ref.inp`** (UK distribution network resaved through EPANET-JS; copied from the build folder used during development). 137 junctions, 2 reservoirs, 1 tank `T1` (`Elev = 79`, `InitLevel = 7.45`, `MinLevel = 0`, `MaxLevel = 7.5`, `Diameter = 20`), one FLV `V2 J137 T1 300 FLV 2 4` (Setting = 2, MinorLoss = 4, no curve, no InletHeight in the legacy form), one PRV, one closed pump, demand patterns and 24-hour duration.

The UK fixture has two purposes:

1. **Realistic regression**: parsing it with `InletHeight` defaulted to `0` must produce exactly the same hydraulic result as the existing FLV implementation does today — i.e. *no* observable change for legacy networks.
2. **End-to-end verification of the new logic**: editing the FLV line to add an `InletHeight` and varying the tank's `InitLevel` lets the implementer eyeball-verify that the tank level converges to the expected band, the inlet flow drops as the tank approaches `top = inlet_height`, and the valve closes cleanly. The runner is `./build/bin/runepanet flv_uk_ref.inp out.rpt out.bin` with `[REPORT]` set to log tank level, valve flow, and valve status. Quick experiments to run during bring-up: (a) `InletHeight = 5, InitLevel = 3` — band sits 4–5 m, expect modulation as the tank fills through 4 m; (b) `InletHeight = 5, InitLevel = 6` — water above inlet, valve is closed via natural closure, tank drains through demand only; (c) `InletHeight = 8` (above `MaxLevel = 7.5`) — always cascade, regulation falls back to TWL band 5.5–7.5 m. The new logic is correct iff each of these matches hand-calculated expectations.

### 9.2 Parser tests

| Test ID  | Description                                                              | Expected                                      |
| -------- | ------------------------------------------------------------------------ | --------------------------------------------- |
| PI-01    | Parse 7-token FLV (legacy, no curve, no InletHeight).                    | `InletHeight == 0`. Behaviour unchanged.      |
| PI-02    | Parse 8-token FLV with InletHeight, no curve.                            | `InletHeight` matches input; curve = none.    |
| PI-03    | Parse 9-token FLV with InletHeight + curve.                              | Both match input.                             |
| PI-04    | Parse FLV with `InletHeight < 0`.                                        | Parser returns "out of range" error.          |
| PI-05    | Parse FLV with `InletHeight == 0` explicit (8-token form).               | Parses; behaviour identical to PI-01.         |
| PI-06    | Parse FLV with `InletHeight > MaxLevel` (e.g. 6.0).                      | Parses (legitimate cascade-only configuration). Optional warning per §11. |
| PI-07    | Parse FLV with `InletHeight == MaxLevel` (boundary).                     | Parses.                                       |

### 9.3 Hydraulic tests

`MaxLevel = 5`, `MinLevel = 0`, regulating range = 0.5 (so `top = 5`, `bot = 4.5`).

| Test ID  | Setup                                                                  | Expected                                                                                              |
| -------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| HI-01    | `InletHeight = 0`, InitLvl = 4.0.                                      | Bit-identical to current FLV H-02. Submerged mode for the entire run.                                 |
| HI-02    | `InletHeight = 4.5`, InitLvl = 4.0 (water below inlet).                | Cascade mode at step 0. Valve sees downstream head `inlet_elev = 104.5`.                              |
| HI-03    | `InletHeight = 4.5`, InitLvl = 4.5 (water at inlet, boundary).         | Boundary case — submerged (mode condition is `H_T >= inlet_elev`).                                    |
| HI-04    | `InletHeight = 4.5`, InitLvl = 4.6 (water above inlet).                | Submerged. Identical to FLV without inlet height in the corresponding band depth.                     |
| HI-05    | `InletHeight = 6.0` (above MaxLevel), InitLvl = 4.0.                   | Always cascade mode. Tank fills up to MaxLevel via cascade.                                           |
| HI-06    | `InletHeight = 4.5`, J1 fixed-head reservoir at H = 104.0 (below inlet_elev). | Cascade mode, but J1 head < inlet_elev. Valve closes (CV reverse-flow check). Q = 0.            |
| HI-07    | `InletHeight = 4.5`, J1 head = 104.6 (just above inlet_elev), tank starts at 4.0. | Cascade mode. Small positive Q determined by valve resistance and `(H_J1 - inlet_elev)`.    |
| HI-08    | `InletHeight = 4.5`, full 24-hour run. InitLvl = 4.0.                  | Tank fills in cascade mode until water surface reaches 4.5; thereafter switches to submerged mode. Mode flag flips on the step where `H_T` first crosses `inlet_elev`. |
| HI-09    | `InletHeight = 0`, run the existing flv_ref.inp suite.                 | All existing FLV tests pass unchanged.                                                                |
| HI-10    | `InletHeight = 3.0`, Setting = 1.0, InitLvl = 2.0.                     | Band is 2.0–3.0 (depths). Modulation as tank fills through the band; valve closes at depth 3.0.       |
| HI-11    | `InletHeight = 3.0`, Setting = 1.0, InitLvl = 4.0 (above inlet).       | Step 0: depth (4.0) >= top (3.0). Status = CLOSED via natural closure. No modulation.                 |
| HI-12    | `InletHeight = 6.0` (above MaxLevel), Setting = 0.5, InitLvl = 4.0.    | `reg_height` falls back to MaxLevel = 5.0. Band is 4.5–5.0 (legacy-equivalent), cascade always active. |
| HI-13    | `InletHeight = 0`, Setting = 0.5, InitLvl = 4.0 (legacy regression).   | Bit-identical to FLV in commit `c5a89f0`. Band is 4.5–5.0, no cascade.                                |

### 9.4 Cascade reverse-flow tests

| Test ID  | Setup                                                                  | Expected                                                                                              |
| -------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| CV-01    | Cascade mode, J1 head < inlet_elev.                                    | Valve status = `CLOSED` at step end. Q = 0.                                                           |
| CV-02    | Submerged mode, configure tank head > J1 head.                         | Bidirectional flow allowed (confirms CV semantics are CASCADE-only, not always-on).                   |
| CV-03    | Cascade mode, J1 head oscillates around inlet_elev between steps.      | Valve flips between ACTIVE and CLOSED step-by-step. No hysteresis (per §2 non-goals).                 |

### 9.5 Round-trip tests

| Test ID  | Description                                                            | Expected                                                                                              |
| -------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| RI-01    | `InletHeight = 0`, no curve. Parse → save → parse.                     | Saved file emits 7 tokens.                                                                            |
| RI-02    | `InletHeight = 2.0`, no curve. Parse → save → parse.                   | Saved file emits 8 tokens.                                                                            |
| RI-03    | `InletHeight = 2.0`, with curve. Parse → save → parse.                 | Saved file emits 9 tokens. Both networks identical.                                                   |
| RI-04    | `InletHeight = 0`, with curve. Parse → save → parse.                   | Saved file emits 9 tokens with explicit `0` in slot 8.                                                |

### 9.6 Numerical reference values

For `inlet_elev = 104.5` (`Elev = 100`, `MinLevel = 0`, `InletHeight = 4.5`), and a J1 reservoir at `H_J1 = 105.0`:

- Cascade flow at full open (pctOpen = 100): `Q = sqrt((H_J1 - inlet_elev) / R)` where `R = K_open`. Substituting: `Q = sqrt(0.5 / 5) = 0.316...` (project flow units; wrap with `Ucf` correctly).

These hand-checks are §10 worked examples, not a test (the test asserts the C result).

---

## 10. Worked Examples

### Example 1: Standard submerged inlet (legacy behaviour preserved)

```
[VALVES]
FV1  J1  T1  150  FLV  0.5  5
```

`InletHeight` defaults to `0`. Identical to today's FLV.

### Example 2: Inlet partway up the tank

```
[VALVES]
FV1  J1  T1  150  FLV  0.5  5  3.0
```

Tank `T1` has `Elev = 100`, `MaxLevel = 5`, `MinLevel = 0`. Inlet sits 3.0 m above the tank floor, so `inlet_elev = 103.0`.

- Tank starts at depth 2.0 (water surface 102.0): below `inlet_elev`. Cascade mode. Valve sees downstream head 103.0.
- Tank rises through cascade until water surface hits 103.0 (= inlet_elev). Mode flips to submerged at the next step.
- Tank continues to fill in submerged mode until pctOpen modulation kicks in at depth 4.5 (the band lower bound for `Setting = 0.5`).

### Example 3: Inlet over the tank rim (always cascade)

```
[VALVES]
FV1  J1  T1  150  FLV  0.5  5  6.0
```

`InletHeight = 6.0`, `MaxLevel = 5`. Inlet is always above the water surface. Valve is always in cascade mode. Tank still naturally closes when water surface reaches MaxLevel (`flvstatus` `Stat = CLOSED` branch dominates; mode flag is irrelevant when the valve is closed).

This models a typical Cla-Val style float-controlled inlet that discharges into the top of the tank.

### Example 4: Cascade with insufficient upstream head

J1 is fed from a reservoir whose head is 102.0. `InletHeight = 4.0` (so `inlet_elev = 104.0`).

- At every step, `H_J1 < inlet_elev`. Cascade-mode reverse-flow check trips on the first iteration. Valve closes. Q = 0.
- Tank does not fill from this valve — the user has under-specified the upstream side. Expected and correct.

### Example 5: regulating band tracks the inlet

```
[VALVES]
FV1  J1  T1  150  FLV  0.5  5  4.7
```

`MaxLevel = 5.0`, `InletHeight = 4.7`. By the new rule, `reg_height = 4.7` (since `0 < 4.7 <= 5.0`), so `top = 100 + 4.7 = 104.7`, `bot = top - 0.5 = 104.2`. `inlet_elev = 104.7`.

Notice that `top == inlet_elev` here — that's not a coincidence, it's the consequence of putting the float at the inlet. So whenever the valve is modulating, depth is below the inlet, which means CASCADE mode.

| Tank depth (m) | H_T   | Stat   | pctOpen          | Mode     | Notes                                          |
| -------------- | ----- | ------ | ---------------- | -------- | ---------------------------------------------- |
| 4.0            | 104.0 | ACTIVE | 100 (below band) | CASCADE  | Fully open. Downstream head 104.7.             |
| 4.2            | 104.2 | ACTIVE | 100 (band lower) | CASCADE  | Fully open. Downstream head 104.7.             |
| 4.4            | 104.4 | ACTIVE | 60               | CASCADE  | 60% open. Downstream head 104.7.               |
| 4.6            | 104.6 | ACTIVE | 20               | CASCADE  | 20% open. Tiny flow.                            |
| 4.7            | 104.7 | CLOSED | —                | SUBMERGED-boundary | Natural closure (depth = top).             |
| 5.0            | 105.0 | CLOSED | —                | SUBMERGED | Already closed.                                |

Compare to the legacy FLV (no InletHeight): the band would be 4.5–5.0, and modulation would happen with the inlet submerged, against the tank's own head. The new rule pushes both the band and the boundary head down to the inlet. The two mechanisms compose cleanly — pctOpen scales `link->R`, mode picks the downstream boundary head.

### Example 6: real UK distribution network (`tests/data/flv_uk_ref.inp`)

A 137-junction UK-style network with one tank `T1` (`Elev = 79`, `MaxLevel = 7.5`) fed by a single FLV from junction `J137` (elevation 74.9, just below the tank floor). The legacy line is:

```
[VALVES]
V2  J137  T1  300  FLV  2  4
```

i.e. Setting = 2 m regulating range, MinorLoss = 4, no curve, no InletHeight. Default behaviour: regulation between depths 5.5 m and 7.5 m, no cascade ever.

To exercise the new logic, replace the line with:

```
[VALVES]
V2  J137  T1  300  FLV  2  4  5
```

Now `InletHeight = 5 m`, so `reg_height = 5` (less than `MaxLevel = 7.5`), `top = 79 + 5 = 84`, `bot = 84 - 2 = 82`, `inlet_elev = 84`. The valve modulates between depths 3 m and 5 m, in CASCADE mode the whole time. The float closes around depth 5 m.

Manual verification recipe (used during implementation bring-up):

1. Run the file with `InitLevel = 3.0` and `[REPORT]` configured to log tank level and valve flow:
   ```
   ./build/bin/runepanet tests/data/flv_uk_ref.inp out.rpt out.bin
   ```
   Expect: tank fills monotonically, valve flow stays at the fully-open rate while depth < 3 m, decreases as depth rises through 3–5 m, drops to 0 at depth = 5 m. Tank level should sit at ~5 m thereafter, draining slowly under demand.

2. Re-run with `InitLevel = 6.0` (above the inlet). Expect: valve closes immediately (`H_T > top`), tank drains under demand only, until depth crosses 5 m at which point modulation resumes.

3. Re-run with `InletHeight = 8` (above `MaxLevel = 7.5`). Expect: regulation falls back to TWL band 5.5–7.5 m; tank fills via cascade, valve closes at MaxLevel.

The implementer should automate these as Boost.Test cases reading the binary output.

---

## 11. Open items the implementer should flag, not decide

These should be raised as questions during review or PR rather than invented:

1. **Mode-switch hysteresis** — if `H_T` oscillates around `inlet_elev` between steps (unlikely but possible), the mode flag will flap. Per §2 non-goals we accept this; an implementer who finds it problematic in practice should propose a small dead-band as a follow-on.

3. **API surface** — explicitly deferred. When we add it, candidates are `EN_INLETHEIGHT` on `EN_setlinkvalue` / `EN_getlinkvalue`. The setting should be readable any time and writable pre-sim only initially (mid-sim writes raise the question of whether mode should re-evaluate inside the current step).

4. **Value of warning threshold for very large `InletHeight`** — e.g. `InletHeight > 2 * MaxLevel`. Optional. If kept, the threshold and message text are the implementer's call; reuse the generic warning slot.

5. **Cascade flow energy losses beyond the valve's minor loss** — drafted as none. If future calibration data shows this matters, a follow-on spec adds it.

6. **Output reporting** — should the simulation status report distinguish SUBMERGED from CASCADE in valve status output? Drafted as no (Mode is not surfaced in `[REPORT]`); the implementer can add a debug-only print if useful during bring-up.

7. **Migration of test fixtures from commit `c5a89f0`** — the grammar reorder (InletHeight before Curve) breaks any existing 8-token FLV lines that supplied a curve. The fixtures shipped in that commit must be updated to the new 9-token form during the implementation. There are no other in-the-wild FLVs to migrate, since FLV is unreleased.

8. **Whether the `reg_height` rule's discontinuity at `InletHeight == 0` is acceptable** — the rule is `reg_height = MaxLevel` at `InletHeight == 0` and `reg_height = InletHeight` at `0 < InletHeight <= MaxLevel`. This is a step discontinuity at zero (`reg_height` jumps from `MaxLevel` down to `0+epsilon`). Drafted as deliberate: `InletHeight == 0` means "legacy / not specified", not "inlet at the floor". The implementer should confirm; an alternative interpretation is to make `InletHeight == 0` literally mean "regulate at floor" (which would render the valve almost always closed) but that breaks legacy compatibility.
