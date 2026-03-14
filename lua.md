# Lua Scripting in EPANET (Branch Feature Overview)

This branch adds embedded Lua scripting so EPANET simulations can inspect network state and make targeted runtime adjustments while hydraulics are running.

The goal is to make simulation behavior more programmable without changing EPANET core input semantics for the rest of the model.

## What is being added

- A built-in Lua runtime linked into EPANET.
- A new `[SCRIPT]` section in INP files for Lua code.
- Script execution during hydraulic simulation steps.
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

At a high level:

1. Lua is initialized when the project/simulation is opened.
2. The script is executed during hydraulic runs (per hydraulic run step).
3. Lua resources are cleaned up when the project is closed.

This allows script logic to react to changing simulated conditions over time.

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
- This feature is actively evolving; API coverage is expected to expand over time.

## Summary

Lua scripting adds a flexible way to observe and influence hydraulic behavior during simulation, directly from INP-authored script blocks. The current implementation is intentionally minimal, with a clear path for expanding properties and helper functions as the feature matures.