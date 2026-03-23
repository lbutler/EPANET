# Lua Scripting in EPANET (Branch Feature Overview)

This branch adds embedded Lua scripting so EPANET simulations can inspect network state and make targeted runtime adjustments while hydraulics are running.

The goal is to make simulation behavior more programmable without changing EPANET core input semantics for the rest of the model.

## What is being added

- A built-in Lua runtime linked into EPANET.
- A new `[SCRIPT]` section in INP files for Lua code.
- Script execution after each hydraulic solve converges, so scripts always see stable, balanced results.
- An event system (`on_event`) for running code at specific simulation lifecycle points.
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

There are two mechanisms for script execution: **top-level code** and the **`on_event` function**.

### Top-level code

Any code written directly in the `[SCRIPT]` section (outside of function definitions) runs **once after the hydraulic solver has converged** at each timestep. The script sees fully balanced, stable results (pressures, flows, etc.) rather than intermediate estimates.

Any changes the script makes to link properties (e.g. `link.status = 0`) are applied and take effect on the **next timestep**. The solver does not re-run for the current timestep. This is the simplest and safest way to add reactive logic.

### The `on_event` function

For finer control over _when_ code executes, define a function called `on_event(event)` in your script. EPANET calls this function at specific lifecycle points, passing a string that identifies the event:

| Event         | When it fires                                                                                                                  |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `"init"`      | Once, when the project is opened and the Lua state is initialized                                                              |
| `"iteration"` | After the hydraulic solver converges; if it changes anything, the solver re-converges and fires again (up to 10 passes)         |
| `"report"`    | After each successful hydraulic solve (including any iteration-event re-solves), before advancing to the next time step         |
| `"term"`      | Once, when the project is closed, before the Lua state is destroyed                                                            |

If `on_event` is not defined, EPANET silently skips the event dispatch.

### Example: using `on_event`

```lua
-- Reserved function called by the EPANET engine
function on_event(event)

    if event == "init" then
        -- Runs ONCE before the simulation starts
        print("Simulation starting...")

    elseif event == "iteration" then
        -- Runs after convergence; changes trigger re-solve
        if node("J1").pressure < 20 then
            link("P1").status = 0
        end

    elseif event == "report" then
        -- Runs after each successful hydraulic step
        print("Time step complete. Pressure at J1 is: ", node("J1").pressure)

    elseif event == "term" then
        -- Runs ONCE after the simulation finishes
        print("Simulation finished. Cleaning up.")
    end
end
```

### Lifecycle

1. Lua is initialized when the project is opened (`EN_open` / `EN_openX`).
2. The script text is executed once to define `on_event` and any globals.
3. The `"init"` event fires immediately after.
4. During each hydraulic timestep:
   a. The solver iterates to convergence (no Lua code runs during iterations).
   b. After convergence, `on_event("iteration")` fires (if defined). If it changed any link status or setting, the solver re-runs to convergence and the event fires again. This repeats until no changes are made (up to 10 passes).
   c. Top-level script code executes once with stable results. Any changes apply to the next timestep.
5. After the solve completes (including any iteration-event re-solves), the `"report"` event fires.
6. When the project is closed, the `"term"` event fires, then the Lua state is destroyed.

### Top-level code execution

Top-level code runs once after the hydraulic solver has fully converged (and after any `on_event("iteration")` re-solves). All node pressures, demands, heads, and link flows represent the balanced solution.

Any changes the script makes take effect on the next timestep, not the current one. The solver does not re-run. This makes top-level code safe for logging, monitoring, and making adjustments that should be applied going forward.

### The `"iteration"` event

The `"iteration"` event runs after each successful convergence. Unlike top-level code, if the event handler modifies a link property, EPANET re-runs the hydraulic solver to convergence and fires the event again. This loop continues until the handler makes no further changes, up to a safety limit of 10 passes.

Because the event always fires after convergence, the script sees stable, balanced values — not intermediate estimates. This avoids the risk of scripts reacting to premature values that may be far from the true solution.

Use `on_event("iteration")` when you need to make adjustments that must be reflected in the **current** timestep's results (e.g. adjusting a PRV setting to hit a target pressure at a remote node).

### Interaction with convergence

If an `on_event("iteration")` handler writes to a link property (e.g. `link.status = 0`) EPANET detects this and re-runs the solver to convergence with the new configuration. The event fires again after re-convergence, giving the script a chance to make further adjustments or confirm the result is satisfactory. This continues until either no changes are made or 10 passes are reached.

Top-level code changes do not trigger re-solving — they are applied and take effect on the next timestep.

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

### 5) Change a link setting (applies next timestep)

```lua
local v = link("V1")
v.setting = 0.75
```

### 6) Open/close style control by status (applies next timestep)

```lua
local v = link("V1")
v.status = 0
```

### 7) Conditional action from pressure (applies next timestep)

```lua
if node("J1").pressure < 20 then
    link("P1").status = 0
end
```

### 8) Adjust a PRV to hit a target pressure (re-solves within current timestep)

```lua
function on_event(event)
    if event == "iteration" then
        local target = 30
        local diff = node("J126").pressure - target
        if math.abs(diff) > 0.01 then
            link("V1").setting = link("V1").setting - diff
        end
    end
end
```

### 9) Full event-driven script

```lua
function on_event(event)
    if event == "init" then
        print("Simulation starting...")
    elseif event == "iteration" then
        if node("J1").pressure < 20 then
            link("P1").status = 0
        end
    elseif event == "report" then
        print("Step done. J1 pressure:", node("J1").pressure)
    elseif event == "term" then
        print("Simulation finished.")
    end
end
```

## Current limits and expectations

- The exposed Lua API is currently a focused subset, not the full Toolkit surface.
- Node properties are currently read-only in scripting.
- Link writes are currently limited to `status` and `setting`.
- Top-level code runs once per timestep after convergence. Changes apply to the next timestep.
- The `"iteration"` event runs after convergence; if it changes anything, the solver re-converges and fires the event again (up to 10 passes per timestep).
- The `"init"` and `"term"` events fire exactly once per project open/close. The `"report"` event fires once per successful hydraulic solve.
- A script that unconditionally toggles a link status in `on_event("iteration")` may cause repeated re-solves. The engine limits this to 10 passes per timestep to prevent infinite cycling.
- This feature is actively evolving; API coverage is expected to expand over time.

## Summary

Lua scripting adds a flexible way to observe and influence hydraulic behavior during simulation, directly from INP-authored script blocks. All Lua code runs after the solver converges, ensuring scripts always see stable, balanced results. Top-level code runs once per timestep with changes applied to the next timestep. The `on_event("iteration")` handler provides a way to make adjustments that are re-solved within the current timestep. Other lifecycle events (`"init"`, `"report"`, `"term"`) fire at their respective points. The current implementation is intentionally minimal, with a clear path for expanding properties and helper functions as the feature matures.
