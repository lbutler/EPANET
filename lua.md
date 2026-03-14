# Lua Scripting in EPANET (Branch Feature Overview)

This branch adds embedded Lua scripting so EPANET simulations can inspect network state and make targeted runtime adjustments while hydraulics are running.

The goal is to make simulation behavior more programmable without changing EPANET core input semantics for the rest of the model.

## What is being added

- A built-in Lua runtime linked into EPANET.
- A new `[SCRIPT]` section in INP files for Lua code.
- Script execution during hydraulic solver iterations, alongside EPANET's own control checks.
- A small, focused Lua API for reading selected node/link values and setting selected link controls.

## Build integration

The CMake build now pulls and builds Lua as part of the EPANET build process, then links it into the EPANET library.

In practice, this means Lua support is part of the normal build flow for this branch rather than a separate plugin or external runtime setup.

## INP structure: `[SCRIPT]`

Lua code is authored directly inside a `[SCRIPT]` section of the INP file.

- Lines under `[SCRIPT]` are collected as script text.
- Script content continues until the next INP section header.
- This keeps scripting alongside the model definition in a single input file.

Example layout:

```ini
[SCRIPT]
print("Lua enabled")
local j = node("J1")
print("Pressure", j.pressure)

[END]
```

## When scripts run

Lua scripts run **inside the hydraulic solver iteration loop**, at the same points where EPANET evaluates its own controls. This means scripts see intermediate solver state and can influence convergence, rather than only reacting after a timestep has fully solved.

### Lifecycle

1. Lua is initialized when the project/simulation is opened.
2. The script is executed during hydraulic solver iterations (see below).
3. Lua resources are cleaned up when the project is closed.

### Execution during iterations

The hydraulic solver (`hydsolve`) iterates to find a converged solution at each timestep. During this loop, EPANET performs status checks on links (pumps, valves, pipes to tanks) and pressure-based controls. The Lua script runs at the same two moments:

**1. On convergence (full status check)**

When the solver converges, it performs a complete status sweep:
- `valvestatus()` — check/update control valve states
- `linkstatus()` — check pumps, CVs, FCVs, tank connections
- `pswitch()` — apply pressure-based simple controls
- **`luascript_run()`** — execute the Lua script

If any of these (including the Lua script) changes a link status or setting, the solver marks that a state change occurred and continues iterating to re-converge with the new configuration. If nothing changes, the solver is done.

**2. On periodic check (non-converged iterations)**

While the solver has not yet converged, periodic status checks run every `CHECKFREQ` iterations (default: 2), up to iteration `MAXCHECK` (default: 10):
- `linkstatus()` — check pumps, CVs, FCVs, tank connections
- **`luascript_run()`** — execute the Lua script

This prevents the script from firing on every single iteration, reducing the risk of oscillation while still giving it regular opportunities to intervene.

### Interaction with convergence

If a Lua script writes to a link property (e.g. `link.status = 0`), EPANET detects this and treats it as a status change. At the convergence check point, this forces the solver to continue iterating so the network can re-converge with the script's modifications applied. This is the same mechanism used for EPANET's own pressure-based controls (`pswitch`).

### Options that affect script execution frequency

| Option | Default | Effect on Lua |
|---|---|---|
| `CHECKFREQ` | 2 | Script runs every N iterations during non-converged periodic checks |
| `MAXCHECK` | 10 | Periodic checks (and script execution) stop after this iteration |
| `MAXITER` | 200 | Normal iteration limit; script does not run during extra iterations |
| `EXTRA ITERATIONS` | -1 | If > 0, extra iterations run with all status changes frozen — Lua is not executed |

### What scripts do NOT see

- Scripts do **not** run once per timestep before or after solving. They only run inside the iteration loop.
- Scripts do **not** run during extra iterations (beyond `MAXITER`), where EPANET freezes all link statuses to attempt final convergence.
- Time-based and tank-level simple controls (`controls()`) and rule-based controls (`checkrules()`) are evaluated outside the iteration loop and are unaffected by Lua.

## Lua API (current scope)

The current API is intentionally small and practical.

- `print(...)`: writes script messages through EPANET reporting output.
- `node("ID")`: returns a node object.
- `link("ID")`: returns a link object.

### Readable node properties

- `node.pressure`
- `node.demand`
- `node.head`

### Readable link properties

- `link.flow`
- `link.velocity`
- `link.status`
- `link.setting`

### Writable link properties

- `link.status = ...`
- `link.setting = ...`

## Tiny usage examples

### 1) Basic logging

```lua
print("Lua script active")
```

### 2) Read a node value

```lua
local j = node("J1")
print("J1 pressure", j.pressure)
```

### 3) Read multiple node properties

```lua
local j = node("J1")
print(j.pressure, j.demand, j.head)
```

### 4) Read link hydraulics

```lua
local p = link("P1")
print("Flow", p.flow, "Velocity", p.velocity)
```

### 5) Change a link setting

```lua
local v = link("V1")
v.setting = 0.75
```

### 6) Open/close style control by status

```lua
local v = link("V1")
v.status = 0
```

### 7) Conditional action from pressure

```lua
if node("J1").pressure < 20 then
    link("P1").status = 0
end
```

## Current limits and expectations

- The exposed Lua API is currently a focused subset, not the full Toolkit surface.
- Node properties are currently read-only in scripting.
- Link writes are currently limited to `status` and `setting`.
- Scripts run inside the iteration loop and can execute many times per timestep (not once). Script logic should be lightweight and idempotent where possible.
- A script that unconditionally toggles a link status may cause the solver to cycle without converging. Use the `CHECKFREQ` and `MAXCHECK` options to limit how often the script can intervene. The solver will stop trying periodic checks (including Lua) after `MAXCHECK` iterations.
- Script execution is suppressed during extra iterations (when `EXTRA ITERATIONS > 0` and `iter > MAXITER`), matching the behavior of EPANET's own controls.
- `DampLimit` does not currently gate Lua execution. The script runs regardless of the current convergence error level.
- This feature is actively evolving; API coverage is expected to expand over time.

## Summary

Lua scripting adds a flexible way to observe and influence hydraulic behavior during simulation, directly from INP-authored script blocks. Scripts run inside the hydraulic solver iteration loop alongside EPANET's own control checks, governed by the `CHECKFREQ` and `MAXCHECK` options. If a script modifies a link, the solver re-converges with the change applied. The current implementation is intentionally minimal, with a clear path for expanding properties and helper functions as the feature matures.
