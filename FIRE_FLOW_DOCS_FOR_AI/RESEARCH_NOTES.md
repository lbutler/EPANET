# Research Notes

## Understanding the Hernández Fire Flow Algorithm

### Key Algorithm Components

1. **Fire Flow Types**:
   - Required flow: sufficient to extinguish fires
   - Available flow: maximum extractable with minimum pressure
   - Design flow: maximum available without violating service constraints (THIS IS WHAT WE'RE IMPLEMENTING)

2. **Single-Loop Approach**:
   - Replaces traditional nested 3-loop approach (outer: critical elements, middle: flow guesses, inner: hydraulics)
   - Integrates all three aspects into the existing GGA loop
   - Significant performance improvement especially with velocity constraints (3.17x speedup)

3. **Core Modifications to GGA**:
   - **Step 2**: Add fire flow guess generation before computing matrix coefficients
   - **Step 4**: After computing flows/velocities, identify critical element using relative closeness
   - **Step 5**: Add fire flow convergence check alongside hydraulic convergence

### Algorithm Details from Paper

#### Fire Flow Guess Generation (Step 2)
Two methods working in tandem:

1. **Physically-based approach** (primary):
   - Uses quadratic regression: Q = c1·x² + c2·x + c3
   - x = critical variable (pressure or velocity)
   - Fits last 3 (Q, x) pairs using Cholesky decomposition
   - Generates new Q by plugging in threshold value x*

2. **Heuristic backup** (when physical fails):
   - Tracks direction changes: d_i = sign(Q_i - Q_i-1)
   - Multiplier: m_i = 1.1 if same direction, -0.4 if direction changed
   - Q_i+1 = Q_i + m_i·ΔQ_i-1

#### Critical Element Selection (Step 4)
- Calculate relative closeness: r_j = (x_j - x*_j) / range_of_variation
- Critical element: j_crit = argmin(r_j)
- r_j < 0 means constraint violation
- Works for both pressure (junctions) and velocity (pipes) constraints

#### Convergence Criteria (Step 5)
Two conditions must be met:
1. Fire flow convergence: |ΔQ_i| < ε
2. Constraint satisfaction: r_j_crit ≥ 0

### Target Accuracy (Table 1 - Small Network)
- Pressure MAE: 0.00141 psi (vs legacy 0.01587) - 91% improvement
- Pressure RMAE: 0.0070% (vs legacy 0.0794%) - 91% improvement

## EPANET Code Structure Analysis

### Global Gradient Algorithm Implementation

The GGA is implemented in `hydsolver.c` with the main loop in the `hydsolve()` function (lines 114-189).

#### Current 5-Step Structure:
1. **Step 1**: `headlosscoeffs(pr)` (line 121) - Computes P and Y coefficients for links
2. **Step 2**: `matrixcoeffs(pr)` (line 122) - Computes matrix coefficients including demands
3. **Step 3**: `linsolve(sm, net->Njuncs)` (line 123) - Solves linear system for heads
4. **Step 4**: `newflows(pr, &hydbal)` (line 139) - Updates flows based on new heads
5. **Step 5**: `hasconverged(pr, relerr, &hydbal)` (line 165) - Checks convergence

### Key Data Structures

#### In `types.h`:
- `Network`: Contains nodes, links, junctions, etc.
- `Hydraul`: Contains hydraulic state (heads, flows, demands, etc.)
- `Smatrix`: Sparse matrix for linear system

#### Important Arrays in Hydraul:
- `NodeHead[]`: Computed heads at nodes
- `LinkFlow[]`: Computed flows in links
- `NodeDemand[]`: Total demand at nodes
- `DemandFlow[]`: Actual delivered demand (for PDA)
- `EmitterFlow[]`: Flow from emitters
- `LeakageFlow[]`: Leakage flows

### Functions to Understand Further

1. **headlosscoeffs()** (hydcoeffs.c:241-286):
   - Computes P (1/gradient) and Y (head loss/gradient) for each link
   - Handles different link types (pipes, pumps, valves)

2. **matrixcoeffs()** (hydcoeffs.c:289-320):
   - Builds the linear system matrix
   - Calls: linkcoeffs(), emittercoeffs(), demandcoeffs(), nodecoeffs(), valvecoeffs()
   - THIS IS WHERE WE'LL ADD FIRE FLOW DEMAND

3. **newflows()** (hydsolver.c:358-):
   - Updates flows after solving for heads
   - Calls subfunctions for link flows, emitter flows, demand flows
   - THIS IS WHERE WE'LL CALCULATE CRITICAL ELEMENT

4. **hasconverged()** (hydsolver.c:655-690):
   - Checks various convergence criteria
   - THIS IS WHERE WE'LL ADD FIRE FLOW CONVERGENCE CHECK

### Integration Points for Fire Flow Algorithm

1. **Before Step 2 (matrixcoeffs)**:
   - Add fire flow guess generation logic
   - Store Q_i, Q_i-1, Q_i-2 and corresponding x values
   - Implement quadratic regression with Cholesky decomposition
   - Implement heuristic backup method

2. **In Step 2 (matrixcoeffs)**:
   - Add fire flow demand to the hydrant node's demand
   - Modify the demand calculation in demandcoeffs()

3. **After Step 4 (newflows)**:
   - Calculate relative closeness for all elements
   - Identify critical element
   - Store critical variable value for next iteration

4. **In Step 5 (hasconverged)**:
   - Add fire flow convergence check: |ΔQ| < ε
   - Add constraint satisfaction check: r_crit ≥ 0
   - Ensure both hydraulic and fire flow convergence

### Key Challenges Identified

1. **Relative Closeness Calculation**:
   - Paper doesn't specify exact normalization method
   - "Range of variation" could mean:
     - Historical min/max range
     - Standard deviation
     - Difference between available and static flow values
   - Will need to experiment and validate against paper's results

2. **Data Structure Modifications**:
   - Need to add fields for:
     - Fire flow history (Q values)
     - Critical variable history (x values)
     - Current critical element index
     - Relative closeness values for all elements
   - Decision: Extend Hydraul struct or create separate FireFlow struct?

3. **Cholesky Decomposition**:
   - Need to implement 3x3 matrix solver
   - Consider numerical stability
   - May be able to reuse/adapt existing matrix solving code

4. **Parameter Values**:
   - Heuristic multipliers (1.1, -0.4) may need tuning
   - Convergence tolerance ε needs to be determined
   - May vary by network characteristics

### Next Steps

1. Study the demand calculation in more detail (demandcoeffs function)
2. Understand how to identify which node is a hydrant
3. Design data structures for fire flow state
4. Create a simple test case to validate implementation
5. Implement quadratic regression with Cholesky decomposition