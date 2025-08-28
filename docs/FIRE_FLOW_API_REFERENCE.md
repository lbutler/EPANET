# EPANET Fire Flow API Reference

## Overview

The Fire Flow API extends EPANET 2.3 with functions for analyzing firefighting water capacity using the Hernández single-loop algorithm.

## Header Files

```c
#include <epanet2.h>
#include <epanet2_2.h>
#include <epanet2_fireflow.h>
```

## Enumerations

### EN_FireFlowType

Specifies the type of fire flow analysis to perform.

```c
typedef enum {
    EN_FF_STATIC = 0,     // Static analysis (no fire flow)
    EN_FF_REQUIRED = 1,   // Required fire flow (not implemented)
    EN_FF_AVAILABLE = 2,  // Available fire flow (not implemented)
    EN_FF_DESIGN = 3      // Design fire flow (implemented)
} EN_FireFlowType;
```

| Value | Description |
|-------|-------------|
| `EN_FF_STATIC` | Baseline hydraulic analysis without fire flow |
| `EN_FF_REQUIRED` | Analysis with required fire flow (future) |
| `EN_FF_AVAILABLE` | Maximum available flow at minimum pressure (future) |
| `EN_FF_DESIGN` | Maximum flow respecting all constraints |

## Functions

### EN_setfireflow

Enables or disables fire flow analysis for the project.

```c
int EN_setfireflow(EN_Project ph, int enabled)
```

#### Parameters
- `ph` - Project handle
- `enabled` - 1 to enable, 0 to disable fire flow analysis

#### Returns
- 0 on success
- Error code on failure

#### Error Codes
- 101 - Insufficient memory
- 102 - No network data available

#### Example
```c
EN_setfireflow(ph, 1);  // Enable fire flow analysis
```

---

### EN_setfireflowhydrant

Sets the hydrant node for fire flow analysis.

```c
int EN_setfireflowhydrant(EN_Project ph, int nodeIndex)
```

#### Parameters
- `ph` - Project handle
- `nodeIndex` - Index of hydrant node (1-based)

#### Returns
- 0 on success
- Error code on failure

#### Error Codes
- 102 - No network data
- 105 - Fire flow not initialized
- 203 - Invalid node index

#### Example
```c
int hydrantIdx;
EN_getnodeindex(ph, "Hydrant1", &hydrantIdx);
EN_setfireflowhydrant(ph, hydrantIdx);
```

#### Notes
- Node must be a junction (not tank or reservoir)
- Only one hydrant can be active at a time

---

### EN_setfireflowparams

Configures fire flow analysis parameters.

```c
int EN_setfireflowparams(EN_Project ph, 
                         double pressureThreshold,
                         double velocityThreshold, 
                         double tolerance)
```

#### Parameters
- `ph` - Project handle
- `pressureThreshold` - Minimum allowable pressure (psi)
- `velocityThreshold` - Maximum allowable velocity (ft/s)
- `tolerance` - Convergence tolerance (gpm)

#### Returns
- 0 on success
- Error code on failure

#### Error Codes
- 102 - No network data
- 105 - Fire flow not initialized
- 202 - Illegal numeric value (negative or zero)

#### Example
```c
EN_setfireflowparams(ph, 
    20.0,   // 20 psi minimum pressure
    10.0,   // 10 ft/s maximum velocity
    5.0     // 5 gpm tolerance
);
```

#### Notes
- Pressure is converted internally from psi to feet of head
- Tolerance affects convergence speed vs. accuracy

---

### EN_runfireflow

Executes fire flow analysis.

```c
int EN_runfireflow(EN_Project ph,
                   EN_FireFlowType analysisType,
                   double *fireFlow,
                   int *criticalNode,
                   int *criticalLink)
```

#### Parameters
- `ph` - Project handle
- `analysisType` - Type of analysis (see EN_FireFlowType)
- `fireFlow` - [out] Calculated fire flow (gpm)
- `criticalNode` - [out] Index of critical node (0 if pipe is critical)
- `criticalLink` - [out] Index of critical link (0 if node is critical)

#### Returns
- 0 on success
- Error code on failure (2 = convergence warning is NOT an error for fire flow)

#### Error Codes
- 102 - No network data
- 103 - Hydraulics not opened
- 105 - Fire flow not initialized
- 203 - No hydrant set
- 251 - Function not implemented (for REQUIRED/AVAILABLE types)

#### Example
```c
double flow;
int critNode, critLink;
char id[256];

EN_openH(ph);
int err = EN_runfireflow(ph, EN_FF_DESIGN, &flow, &critNode, &critLink);

if (err == 0 || err == 2) {  // 2 = convergence warning is OK
    printf("Design fire flow: %.1f gpm\n", flow);
    
    if (critNode > 0) {
        EN_getnodeid(ph, critNode, id);
        printf("Critical element: Node %s\n", id);
    } else if (critLink > 0) {
        EN_getlinkid(ph, critLink, id);
        printf("Critical element: Pipe %s\n", id);
    }
}
EN_closeH(ph);
```

#### Notes
- Hydraulics must be opened before calling
- Convergence warnings (error 2) are normal for fire flow
- Either criticalNode OR criticalLink will be set, not both

---

### EN_getfireflowstatus

Retrieves detailed fire flow analysis status.

```c
int EN_getfireflowstatus(EN_Project ph,
                         int *converged,
                         int *iterations,
                         double *relativeCloseness)
```

#### Parameters
- `ph` - Project handle
- `converged` - [out] 1 if converged, 0 otherwise
- `iterations` - [out] Number of iterations performed
- `relativeCloseness` - [out] Relative closeness of critical element

#### Returns
- 0 on success
- Error code on failure

#### Error Codes
- 102 - No network data
- 105 - Fire flow not initialized

#### Example
```c
int conv, iter;
double rc;

EN_getfireflowstatus(ph, &conv, &iter, &rc);

printf("Status: %s\n", conv ? "Converged" : "Not converged");
printf("Iterations: %d\n", iter);
printf("Critical element closeness: %.4f\n", rc);

if (rc < 0) {
    printf("Warning: Constraint violated\n");
} else if (rc < 0.1) {
    printf("Operating at constraint boundary\n");
}
```

#### Relative Closeness Interpretation
| Value | Meaning |
|-------|---------|
| RC > 0.5 | Well within constraints |
| 0.1 < RC < 0.5 | Moderate margin |
| 0 < RC < 0.1 | Near constraint boundary |
| RC ≈ 0 | At constraint (optimal) |
| -0.05 < RC < 0 | Slight violation (may converge) |
| RC < -0.05 | Significant violation |

## Complete Example Program

```c
#include <stdio.h>
#include <stdlib.h>
#include <epanet2.h>
#include <epanet2_2.h>
#include <epanet2_fireflow.h>

int main(int argc, char *argv[]) {
    EN_Project ph;
    int err, hydrantIdx;
    double flow;
    int critNode, critLink;
    int conv, iter;
    double rc;
    char id[256];
    
    // Create and open project
    EN_createproject(&ph);
    EN_open(ph, argv[1], "", "");
    
    // Configure fire flow
    EN_getnodeindex(ph, argv[2], &hydrantIdx);
    EN_setfireflow(ph, 1);
    EN_setfireflowhydrant(ph, hydrantIdx);
    EN_setfireflowparams(ph, 20.0, 10.0, 5.0);
    
    // Run analysis
    EN_openH(ph);
    err = EN_runfireflow(ph, EN_FF_DESIGN, &flow, 
                        &critNode, &critLink);
    
    if (err == 0 || err == 2) {
        printf("Fire flow at %s: %.1f gpm\n", argv[2], flow);
        
        // Get critical element
        if (critNode > 0) {
            EN_getnodeid(ph, critNode, id);
            printf("Limited by: Node %s (pressure)\n", id);
        } else if (critLink > 0) {
            EN_getlinkid(ph, critLink, id);
            printf("Limited by: Pipe %s (velocity)\n", id);
        }
        
        // Get status
        EN_getfireflowstatus(ph, &conv, &iter, &rc);
        printf("Converged: %s after %d iterations\n", 
               conv ? "Yes" : "No", iter);
        printf("Constraint margin: %.1f%%\n", rc * 100);
    }
    
    // Clean up
    EN_closeH(ph);
    EN_close(ph);
    EN_deleteproject(ph);
    
    return 0;
}
```

## Error Handling

### Common Error Patterns

```c
// Pattern 1: Check each call
int err;
err = EN_setfireflow(ph, 1);
if (err) {
    printf("Failed to enable fire flow: %d\n", err);
    return err;
}

// Pattern 2: Aggregate checking
int err = 0;
err |= EN_setfireflow(ph, 1);
err |= EN_setfireflowhydrant(ph, hydrantIdx);
err |= EN_setfireflowparams(ph, 20.0, 10.0, 5.0);
if (err) {
    printf("Setup failed\n");
    return err;
}

// Pattern 3: Ignore convergence warnings
err = EN_runfireflow(ph, EN_FF_DESIGN, &flow, NULL, NULL);
if (err && err != 2) {  // 2 = convergence warning
    printf("Analysis failed: %d\n", err);
    return err;
}
```

## Thread Safety

The fire flow functions are **NOT thread-safe**. Each thread must use its own EN_Project handle.

```c
// WRONG - Shared project
#pragma omp parallel for
for (int i = 0; i < n; i++) {
    EN_setfireflowhydrant(ph, nodes[i]);  // Race condition!
    EN_runfireflow(ph, ...);
}

// CORRECT - Separate projects
#pragma omp parallel for
for (int i = 0; i < n; i++) {
    EN_Project local_ph;
    EN_createproject(&local_ph);
    EN_open(local_ph, file, "", "");
    EN_setfireflowhydrant(local_ph, nodes[i]);
    EN_runfireflow(local_ph, ...);
    EN_deleteproject(local_ph);
}
```

## Performance Notes

### Typical Iteration Counts
- Simple networks: 5-15 iterations
- Complex networks: 15-30 iterations
- Difficult cases: 30-50 iterations

### Factors Affecting Performance
1. **Network size** - O(n) complexity
2. **Constraint tightness** - Tighter = more iterations
3. **Initial guess quality** - Better guess = faster convergence
4. **Tolerance setting** - Looser = fewer iterations

### Optimization Strategies

```c
// Strategy 1: Warm start with previous result
double lastFlow = 0;
for (int i = 0; i < nHydrants; i++) {
    // TODO: Future API to set initial guess
    // EN_setfireflowinitialguess(ph, lastFlow);
    EN_runfireflow(ph, EN_FF_DESIGN, &lastFlow, ...);
}

// Strategy 2: Progressive refinement
double tolerances[] = {20.0, 10.0, 5.0, 1.0};
for (int i = 0; i < 4; i++) {
    EN_setfireflowparams(ph, 20.0, 10.0, tolerances[i]);
    EN_runfireflow(ph, EN_FF_DESIGN, &flow, ...);
}

// Strategy 3: Parallel hydrant analysis
// See thread safety section above
```

## Limitations

### Current Implementation
- Only `EN_FF_DESIGN` type is implemented
- Single hydrant analysis only
- No warm start capability
- No intermediate results during iteration

### Planned Enhancements
- [ ] Required and Available flow types
- [ ] Multiple simultaneous hydrants
- [ ] Warm start from previous solution
- [ ] Callback for iteration monitoring
- [ ] Constraint relaxation options

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 2.3.01 | 2024 | Initial implementation of Hernández algorithm |

## See Also

- [Fire Flow User Guide](FIRE_FLOW_USER_GUIDE.md)
- [EPANET Programmer's Toolkit](https://github.com/OpenWaterAnalytics/EPANET)
- [Hernández (2024) Paper](https://doi.org/10.1061/JWRMD5.WRENG-6201)