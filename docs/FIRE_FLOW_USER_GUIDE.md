# EPANET Fire Flow Analysis User Guide

## Table of Contents
1. [Introduction](#introduction)
2. [What is Fire Flow Analysis?](#what-is-fire-flow-analysis)
3. [The Hernández Algorithm](#the-hernández-algorithm)
4. [Installation](#installation)
5. [Quick Start](#quick-start)
6. [Detailed Usage](#detailed-usage)
7. [Parameters and Settings](#parameters-and-settings)
8. [Interpreting Results](#interpreting-results)
9. [Troubleshooting](#troubleshooting)
10. [Performance Considerations](#performance-considerations)

## Introduction

This guide describes the fire flow analysis capability added to EPANET 2.3, based on the groundbreaking research by Felipe Hernández (2024): "Fast Firefighting Water Capacity Assessment Using a Streamlined Single-Loop Hybrid Search."

### Key Benefits
- **3x faster** than traditional multi-loop approaches
- **Single-loop integration** with EPANET's Global Gradient Algorithm (GGA)
- **Automatic critical element identification**
- **Respects both pressure and velocity constraints**
- **Improved accuracy** with physically-based quadratic regression

## What is Fire Flow Analysis?

Fire flow analysis determines the maximum flow available at a hydrant location while maintaining minimum service levels throughout the water distribution network.

### Types of Fire Flow

1. **Static Flow**: Normal system flow without fire demand
2. **Required Fire Flow**: Flow needed for firefighting (set by fire codes)
3. **Available Fire Flow**: Maximum possible flow at minimum pressure
4. **Design Fire Flow**: Maximum flow while respecting ALL constraints (pressure AND velocity)

This implementation focuses on **Design Fire Flow**, the most practical metric for system planning.

## The Hernández Algorithm

The algorithm revolutionizes fire flow calculation by:

1. **Single-Loop Integration**: Runs within EPANET's normal hydraulic solver
2. **Hybrid Search Strategy**: Combines:
   - **Heuristic guessing** for initial iterations
   - **Quadratic regression** for refined convergence
3. **Relative Closeness Ranking**: Identifies the most critical element automatically
4. **Smart Convergence**: Stops when constraints are satisfied within tolerance

### How It Works

```
1. Start with initial fire flow guess
2. Run hydraulic analysis
3. Calculate relative closeness for all elements
4. Identify critical element (most constraining)
5. Generate new flow guess based on history
6. Repeat until convergence
```

## Installation

The fire flow capability is built into EPANET 2.3. Simply compile EPANET with the fire flow module:

```bash
mkdir build
cd build
cmake ..
make
sudo make install
```

## Quick Start

### C API Example

```c
#include <epanet2.h>
#include <epanet2_2.h>
#include <epanet2_fireflow.h>

// Create and open project
EN_Project ph;
EN_createproject(&ph);
EN_open(ph, "network.inp", "", "");

// Set up fire flow analysis
int hydrantIndex;
EN_getnodeindex(ph, "Hydrant1", &hydrantIndex);
EN_setfireflow(ph, 1);  // Enable
EN_setfireflowhydrant(ph, hydrantIndex);
EN_setfireflowparams(ph, 20.0, 10.0, 5.0);  // psi, ft/s, gpm

// Run analysis
EN_openH(ph);
double fireFlow;
int criticalNode, criticalLink;
EN_runfireflow(ph, EN_FF_DESIGN, &fireFlow, 
               &criticalNode, &criticalLink);

printf("Design fire flow: %.1f gpm\n", fireFlow);

// Clean up
EN_closeH(ph);
EN_close(ph);
EN_deleteproject(ph);
```

### Python Example (using EPANET Python wrapper)

```python
import epanet as en

# Open network
en.open("network.inp")

# Configure fire flow
hydrant = en.getnodeindex("Hydrant1")
en.setfireflow(1)  # Enable
en.setfireflowhydrant(hydrant)
en.setfireflowparams(20.0, 10.0, 5.0)  # psi, ft/s, gpm

# Run analysis
en.openH()
flow, crit_node, crit_link = en.runfireflow(en.FF_DESIGN)
print(f"Design fire flow: {flow:.1f} gpm")

# Get details
converged, iterations, closeness = en.getfireflowstatus()
print(f"Converged: {converged}, Iterations: {iterations}")

en.closeH()
en.close()
```

## Detailed Usage

### Step 1: Enable Fire Flow Analysis

```c
EN_setfireflow(ph, 1);  // 1 = enable, 0 = disable
```

### Step 2: Select Hydrant Location

```c
int hydrantIndex;
EN_getnodeindex(ph, "NodeID", &hydrantIndex);
EN_setfireflowhydrant(ph, hydrantIndex);
```

### Step 3: Configure Parameters

```c
EN_setfireflowparams(ph, 
    20.0,   // Minimum pressure (psi)
    10.0,   // Maximum velocity (ft/s)
    5.0     // Convergence tolerance (gpm)
);
```

### Step 4: Run Analysis

```c
double fireFlow;
int criticalNode, criticalLink;
EN_runfireflow(ph, EN_FF_DESIGN, &fireFlow, 
               &criticalNode, &criticalLink);
```

### Step 5: Check Results

```c
int converged, iterations;
double relCloseness;
EN_getfireflowstatus(ph, &converged, &iterations, &relCloseness);

if (converged) {
    printf("Converged after %d iterations\n", iterations);
    printf("Critical element closeness: %.4f\n", relCloseness);
}
```

## Parameters and Settings

### Pressure Threshold
- **Units**: PSI (pounds per square inch)
- **Typical**: 20 PSI for residential, 35 PSI for commercial
- **Purpose**: Minimum allowable pressure at any junction

### Velocity Threshold
- **Units**: ft/s (feet per second)
- **Typical**: 8-10 ft/s for distribution, 15 ft/s for transmission
- **Purpose**: Maximum allowable velocity in any pipe

### Convergence Tolerance
- **Units**: GPM (gallons per minute)
- **Typical**: 1-10 GPM
- **Purpose**: Acceptable change between iterations

### Advanced Settings (in code)

```c
// In fireflow.h - can be modified before compilation
#define FF_HEURISTIC_MULT_UP 1.1    // Growth rate when increasing
#define FF_HEURISTIC_MULT_DOWN -0.4  // Back-off rate when decreasing
#define FF_MAX_HISTORY 10            // History points for regression
#define FF_RC_TOLERANCE 0.05         // Relative closeness tolerance
```

## Interpreting Results

### Fire Flow Value
The reported flow is the **maximum sustainable flow** at the hydrant while maintaining all constraints.

### Critical Element
- **Critical Node**: Junction with minimum pressure margin
- **Critical Pipe**: Pipe with minimum velocity margin
- Indicates the system bottleneck

### Relative Closeness (RC)
- **RC > 0**: Constraint satisfied with margin
- **RC ≈ 0**: At constraint boundary (optimal)
- **RC < 0**: Constraint violated

### Convergence Status
- **Converged = Yes**: Solution found within tolerance
- **Converged = No**: May indicate:
  - Infeasible constraints
  - Unstable network
  - Need for parameter adjustment

## Troubleshooting

### Problem: Analysis doesn't converge

**Solutions:**
1. Increase velocity threshold (pipes may be too small)
2. Decrease pressure threshold (system may be weak)
3. Increase convergence tolerance
4. Check for closed valves or disconnected areas

### Problem: Fire flow is zero or very low

**Causes:**
- Constraints are too restrictive
- Hydrant is in low-pressure zone
- Small pipe diameters near hydrant

### Problem: Different results than traditional method

**This is expected!** The Hernández method:
- Considers ALL constraints simultaneously
- Is more accurate than sequential approaches
- May give lower (more conservative) values

### Problem: Slow convergence

**Solutions:**
1. Adjust heuristic multipliers in source code
2. Start with looser constraints
3. Use graduated approach (multiple runs)

## Performance Considerations

### Network Size
- **Small (<1000 pipes)**: <1 second typical
- **Medium (1000-10000 pipes)**: 1-5 seconds typical  
- **Large (>10000 pipes)**: 5-30 seconds typical

### Optimization Tips

1. **Pre-screen hydrants**: Test likely candidates first
2. **Cache results**: Store for similar conditions
3. **Parallel analysis**: Run multiple hydrants simultaneously
4. **Simplify network**: Remove non-critical elements

### Comparison with Traditional Methods

| Method | Loops | Speed | Accuracy | Constraints |
|--------|-------|-------|----------|-------------|
| Traditional | 3+ | Slow | Approximate | Sequential |
| Hernández | 1 | 3x faster | High | Simultaneous |

## Advanced Topics

### Custom Convergence Criteria

Modify `fireFlowCheckConvergence()` in `fireflow.c`:

```c
// Example: Tighter convergence
if (deltaQ < 0.1 && rcCritical >= -0.01 && rcCritical <= 0.01) {
    pr->fireflow->Converged = 1;
    return 1;
}
```

### Multiple Hydrant Analysis

```c
for (int i = 1; i <= numHydrants; i++) {
    EN_setfireflowhydrant(ph, hydrantNodes[i]);
    EN_runfireflow(ph, EN_FF_DESIGN, &flows[i], 
                   &critNodes[i], &critLinks[i]);
    printf("Hydrant %d: %.1f gpm\n", i, flows[i]);
}
```

### Batch Processing

```c
// Test range of thresholds
for (double psi = 15; psi <= 35; psi += 5) {
    EN_setfireflowparams(ph, psi, 10.0, 5.0);
    EN_runfireflow(ph, EN_FF_DESIGN, &flow, NULL, NULL);
    printf("At %g psi: %.1f gpm\n", psi, flow);
}
```

## References

Hernández, F. (2024). "Fast Firefighting Water Capacity Assessment Using a Streamlined Single-Loop Hybrid Search." *Journal of Water Resources Planning and Management*, 150(2), 04023081.

## Support

For issues, questions, or contributions:
- GitHub: [OpenWaterAnalytics/EPANET](https://github.com/OpenWaterAnalytics/EPANET)
- Documentation: This guide
- Paper: See reference above for algorithm details