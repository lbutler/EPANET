## PRV with remote target

```shell
./../build/bin/runepanet demo-prv.inp demo-prv.rpt
```

[View `demo-prv.inp` script](demo-prv.inp#L2464)

This example demonstrates how to use a Lua script within the EPANET simulation to dynamically adjust a pressure reducing valve (PRV) setting based on the pressure measured at a remote junction (J126). Using `on_event("iteration")`, the script runs after each hydraulic solve converges, checks the pressure at J126, and if it is not at the desired target (30m), recalculates and updates the setting of PRV V1. Because the script uses the iteration event, any changes trigger a re-solve so the PRV adjustment takes effect within the current timestep. The script provides printed feedback about pressure levels and valve adjustments as the simulation progresses.

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

This example shows how to use the reserved `on_event` function to run Lua code only at specific times during the simulation.

Without events, top-level code in the script runs once after each timestep's hydraulic solve converges. By implementing `on_event(event)`, you can react to specific lifecycle points:

- **`init`** — Runs once before the simulation starts. Use it for setup or one-off prints (e.g. "Simulation starting...").
- **`iteration`** — Runs after the hydraulic solver converges. If it changes anything, the solver re-converges and the event fires again (up to 10 passes). Use it when you need adjustments reflected in the current timestep's results.
- **`report`** — Runs after each successful hydraulic time step. Use it for per-timestep logging or checks.
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
- **`iteration`** — After the solver converges, compares J126 pressure to the target. If the error is larger than `eps_p`, it computes a new pump speed: if the last two (speed, pressure) points are too similar (which would make the secant step undefined), it uses a simple ratio correction; otherwise it uses a secant step toward the target pressure. The new speed is clamped to [0, maxSpeed] and applied only if the change exceeds `eps_s`; when applied, the current (speed, pressure) is stored for the next secant step. Any change triggers a re-solve so the adjustment is reflected in the current timestep.

So the script runs control logic after convergence (with automatic re-solving to reflect changes) and uses the report step to log and refresh the history used by that control logic.

## Flow-modulating PRV

```shell
./../build/bin/runepanet demo-flow-prv.inp demo-flow-prv.rpt
```

[View `demo-flow-prv.inp` script](demo-flow-prv.inp#L2464)

This example shows how to modulate a PRV's downstream pressure setpoint based on the current flow rate through the valve. A lookup table maps flow (LPS) to target pressure (m) with linear interpolation between entries. Flows outside the table range are clamped to the nearest boundary value.

The table uses four points with deliberately different slopes between segments so the output clearly shows varying interpolation rates:

| Flow (LPS) | Pressure (m) | Slope        |
| ---------- | ------------ | ------------ |
| 3.0        | 20           | —            |
| 5.0        | 24           | 2.0 m per LPS |
| 7.0        | 30           | 3.0 m per LPS |
| 9.0        | 34           | 2.0 m per LPS |

The observed flow range through V1 over the 24-hour demand cycle is roughly 2.5–9.6 LPS, so the table bounds (3.0–9.0) sit just inside the actual range. This lets the example demonstrate clamping at both ends — low flows hold at 20 m, high flows hold at 34 m.

The script uses `on_event("iteration")` to read the flow and set the PRV after each convergence (triggering a re-solve so the new setting is reflected in the current timestep), and `on_event("report")` to print a summary line per timestep.

## Float valve

```shell
./../build/bin/runepanet demo-float-valve.inp demo-float-valve.rpt
```

[View `demo-float-valve.inp` script](demo-float-valve.inp#L72)

This example simulates a float valve controlling inflow to a tank. A float valve progressively closes as the tank level rises, throttling inflow to prevent overflow. The valve is modelled as a TCV (Throttle Control Valve) whose minor-loss coefficient K is adjusted at each timestep based on the current tank water level.

The script works in three steps:

1. **Valve open percentage** — computed from the tank level. The valve is fully open (100%) when the level is below `controlDepth - range` and fully closed (0%) at `controlDepth`. In between, it scales linearly.

2. **K value lookup** — a 9-point characteristic curve maps the open percentage to a minor-loss coefficient K. The curve spans many orders of magnitude (K = 2 when fully open, K ~ 1e20 when closed), so **logarithmic interpolation** is used between curve points rather than linear interpolation.

3. **Apply to TCV** — the computed K is written to the TCV setting inside `on_event("iteration")`, so EPANET re-solves with the updated resistance and the float valve behaviour takes effect within the current timestep.

The network has a reservoir feeding through the TCV into a simple pipe system with a tank and a downstream demand node with a realistic 15-minute demand pattern over 24 hours. The float valve keeps the tank level regulated within a narrow band around the control depth.

## Variable speed pump via PRV method (1-point curve)

```shell
./../build/bin/runepanet demo-vsp-prv-1pt.inp demo-vsp-prv-1pt.rpt
```

[View `demo-vsp-prv-1pt.inp` script](demo-vsp-prv-1pt.inp#L3651)

This example demonstrates an alternative method for controlling a variable speed pump (VSP) using a PRV as a temporary solver constraint. Instead of iteratively searching for the correct pump speed (as in the secant-method `demo-vsp.inp` example), this approach uses the PRV to determine what operating point the pump needs to hit, then calculates the speed algebraically.

The method works in two solver passes per timestep:

1. **Pass 1 (PRV constraining)** — The PRV (V1) is set to the desired downstream pressure (80 m). The solver converges, establishing the flow Q through the pump and all junction heads. The script reads the pump head and PRV head drop, calculates the target pump head (what the pump would need to produce without the PRV), and solves for the pump speed algebraically. It then opens the PRV fully and sets the pump speed, triggering a re-solve.

2. **Pass 2 (verification)** — The solver re-converges with the PRV open and the pump at the calculated speed. The pump naturally produces the correct operating point. No further changes are made.

In the report event, the script logs the final speed and pressures, then resets the PRV to the target pressure for the next timestep. This way the final results reflect pump speed modulation rather than PRV throttling.

The pump curve is a single point (1 LPS, 50 m), which EPANET interprets as H = 66.5 - 16.5·Q² (C = 2). Because the exponent C equals 2, the speed equation simplifies to N = sqrt((H\_target + B·Q²) / A), giving a direct algebraic solution with no iteration required.

## Variable speed pump via PRV method (3-point curve)

```shell
./../build/bin/runepanet demo-vsp-prv-3pt.inp demo-vsp-prv-3pt.rpt
```

[View `demo-vsp-prv-3pt.inp` script](demo-vsp-prv-3pt.inp#L3653)

This example extends the PRV-based VSP control method to a 3-point pump curve, where the curve exponent C ≠ 2 and the speed equation no longer has a closed-form solution.

The pump curve uses three points: (0, 60), (1, 50), (2, 0), which fit to H = 60 - 10·Q^2.585. At speed N, the affinity-law-adjusted curve is H = A·N² - B·Q^C·N^(2−C). With C ≈ 2.585, the exponent (2 − C) ≈ −0.585, making N appear in two terms with different exponents. This requires Newton-Raphson iteration to solve for N:

- f(N) = A·N² − B·Q^C·N^(2−C) − H\_target
- f′(N) = 2·A·N − (2−C)·B·Q^C·N^(1−C)
- N\_next = N − f(N)/f′(N)

The overall two-pass PRV method is identical to the 1-point example — only the speed calculation differs. This demonstrates that the PRV approach works regardless of pump curve complexity, since the flow and head determination is handled by the solver, and only the final speed calculation changes.
