## PRV with remote target

```shell
./../build/bin/runepanet demo-prv.inp demo-prv.rpt
```

[View `demo-prv.inp` script](demo-prv.inp#L2464)

This example demonstrates how to use a Lua script within the EPANET simulation to dynamically adjust a pressure reducing valve (PRV) setting based on the pressure measured at a remote junction (J126). During each hydraulic iteration, the script checks the pressure at J126 and, if it is not at the desired target (30m), automatically recalculates and updates the setting of PRV V1 to maintain the target pressure downstream. The script provides printed feedback about pressure levels and valve adjustments as the simulation progresses.

## Lua kitchen sink example

```shell
./../build/bin/runepanet demo1.inp demo1.rpt
```

[View `demo1.inp` script](demo1.inp#L2464)

This example, "Lua kitchen sink," demonstrates a wide range of Lua scripting features and how they can be used within an EPANET simulation.

The script showcases how you can:

- Define and print variables and perform various mathematical operations
- Use the Lua math library for computations (such as square root, trigonometric functions, random numbers)
- Use control flow structures like loops and conditionals to make simulation decisions
- Query node and link properties (e.g., pressure at a junction, valve setting) and update network elements dynamically
- Define and invoke functions, including nested functions, for custom scripting logic
- Add custom logic that responds to EPANET simulation events (`init`, `iteration`, `report`, and `term`)
- Print custom diagnostics and formatted messages at different stages of the simulation

This example serves as a template demonstrating the flexibility of Lua scripting within EPANET to manipulate and monitor your network during simulation runs.

## On event example

```shell
./../build/bin/runepanet demo2.inp demo2.rpt
```

[View `demo2.inp` script](demo2.inp#L2464)

This example shows how to use the reserved `on_event` function to run Lua code only at specific times during the simulation, instead of on every hydraulic iteration.

Without events, any code in the script that runs during the simulation would execute every iteration. By implementing `on_event(event)`, you can react only when the engine emits a particular event:

- **`init`** — Runs once before the simulation starts. Use it for setup or one-off prints (e.g. "Simulation starting...").
- **`iteration`** — Runs on every hydraulic iteration. Use it when you need to react each time the solver iterates (e.g. to log or adjust when a condition like pressure at J1 < 40 is met).
- **`report`** — Runs after each successful hydraulic time step. Use it for per-timestep logging or checks without running on every iteration.
- **`term`** — Runs once after the simulation finishes. Use it for cleanup or final messages.

## Variable speed pump

```shell
./../build/bin/runepanet demo-vsp.inp demo-vsp.rpt
```

[View `demo-vsp.inp` script](demo-vsp.inp#L3651)

This example shows how to control a variable speed pump (VSP) with Lua so that pressure at a remote junction stays near a target. The script keeps pressure at junction J126 close to 90 m by adjusting the speed setting of pump PU2.

The logic uses `on_event` so that work happens at the right time:

- **`init`** — Sets global parameters once: target pressure (90), pressure and speed tolerances (`eps_p`, `eps_s`), max pump speed (2.0), and initial history variables `s1`, `p1` for the secant method.
- **`report`** — After each successful time step, reads the current PU2 speed and J126 pressure, prints a short step report, and updates the history (`s1`, `p1`) so the next iteration can use the secant formula.
- **`iteration`** — On every hydraulic iteration, compares J126 pressure to the target. If the error is larger than `eps_p`, it computes a new pump speed: if the last two (speed, pressure) points are too similar (which would make the secant step undefined), it uses a simple ratio correction; otherwise it uses a secant step toward the target pressure. The new speed is clamped to [0, maxSpeed] and applied only if the change exceeds `eps_s`; when applied, the current (speed, pressure) is stored for the next secant step.

So the script runs control logic every iteration (to drive pressure toward the target) and uses the report step only to log and to refresh the history used by that control logic.
