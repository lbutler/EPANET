# Detailed Implementation Plan for EPANET Fire Flow Algorithm

## Phase 1: Research and Understanding (Sessions 1-3)

### 1.1 Study the Problem Domain

- Read and analyze the complete Hernández paper to understand:
  - Three fire flow types: required, available, design
  - Traditional multi-loop approach limitations
  - Critical element concept (pressure/velocity constraints)
- Research fire flow analysis fundamentals and regulatory requirements
- Understand why design flow calculation is computationally expensive

### 1.2 Analyze EPANET Architecture

- Examine EPANET source code structure, focusing on:
  - `hydsolver.c` - main hydraulic solver implementation
  - Global Gradient Algorithm (GGA) implementation
  - How the 5-step loop currently operates
- Identify data structures for nodes, links, pressures, velocities
- Understand how demands are currently handled in Step 2
- Map out convergence criteria in Step 5

### 1.3 Understand Baseline Fire Flow Implementation

- Study existing EPANET fire flow capabilities (if any)
- Research InfoWater's traditional three-loop approach mentioned in paper
- Identify what "critical element" selection currently looks like

## Phase 2: Design Implementation Strategy (Sessions 4-5)

### 2.1 Plan Code Modifications

- Map paper's Figure 1 modifications to specific code locations in `hydsolver.c`
- Design data structures needed for:
  - Fire flow guess storage (Q_i, Q_i-1, Q_i-2)
  - Critical variable history (x values corresponding to Q values)
  - Critical element tracking
  - Relative closeness calculations

### 2.2 Design Test Framework

- Plan validation using EPANET's sample network ("Small" model from paper)
- Create test scenarios for pressure-only and pressure+velocity constraints
- Design metrics collection (MAE, RMAE as shown in Table 1)

## Phase 3: Implementation - Core Components (Sessions 6-10)

### 3.1 Implement Fire Flow Guess Generation (Step 2 Modification)

**Session 6-7:**

- Add fire flow demand variable to node demand calculation
- Implement quadratic regression using Cholesky decomposition:
  - Create 3x3 matrix system from last three (Q,x) pairs
  - Solve for coefficients c1, c2, c3 in Q = c1*x² + c2*x + c3
  - Generate new Q by plugging in threshold value x\*

**Session 8:**

- Implement heuristic backup method:
  - Track direction changes (d_i)
  - Apply multiplier logic: m_i = 1.1 if same direction, -0.4 otherwise
  - Override physically-based guess when direction is wrong

### 3.2 Implement Critical Element Selection (Step 4 Modification)

**Session 9-10:**

- After computing pressures/velocities, calculate relative closeness r_j for each element
- **Challenge**: Implement normalization using "range of variation"
  - Try approach 1: (x_j - x\*\_j) / (max_variation - min_variation)
  - Try approach 2: (x_j - x\*\_j) / standard_deviation
  - Try approach 3: (x_j - x\*\_j) / (available_flow_value - static_demand_value)
- Select critical element: j_crit = argmin(r_j)
- Handle both pressure constraints (junctions) and velocity constraints (pipes)

## Phase 4: Implementation - Integration (Sessions 11-13)

### 4.1 Modify Convergence Logic (Step 5 Modification)

**Session 11:**

- Add fire flow convergence check: |ΔQ_i| < ε
- Add constraint satisfaction check: r_j_crit ≥ 0
- Integrate with existing GGA convergence criteria

### 4.2 Integration and Initial Testing

**Session 12-13:**

- Integrate all modifications into single-loop structure
- Handle initialization of Q values from static, required, available flow runs
- Debug compilation and runtime errors
- Test on simple networks first

## Phase 5: Validation and Refinement (Sessions 14-20)

### 5.1 Validate Against Paper Results

**Session 14-16:**

- Test on EPANET sample network (corresponds to "Small" model)
- Compare results to Table 1 benchmarks:
  - Pressure MAE target: 0.00141 psi (vs legacy 0.01587)
  - Pressure RMAE target: 0.0070% (vs legacy 0.0794%)
- If results don't match, iterate on relative closeness calculation method

### 5.2 Parameter Tuning

**Session 17-18:**

- If heuristic multipliers (1.1, -0.4) don't work well, experiment with alternatives
- Tune convergence tolerance ε
- Test robustness across different network configurations

### 5.3 Performance Testing

**Session 19:**

- Measure computation time improvements
- Test on larger networks if available
- Compare iteration counts vs traditional methods

### 5.4 Edge Case Handling

**Session 20:**

- Test networks with pumps and valves (where parabolic assumption may fail)
- Handle cases where critical element shifts during iteration
- Test convergence failure scenarios

## Phase 6: Documentation and Finalization (Sessions 21-22)

### 6.1 Document Implementation

- Create detailed comments explaining algorithm modifications
- Document assumptions made for undefined aspects (relative closeness calculation)
- Note differences from paper implementation

### 6.2 Create Usage Examples

- Develop test cases demonstrating improved accuracy
- Create comparison scripts showing performance vs baseline EPANET

## Critical Decision Points Requiring Human Feedback

1. **Relative closeness normalization method** - Will need validation against known results to determine correct approach
2. **Cholesky decomposition implementation** - May need numerical stability considerations
3. **Parameter values** - Heuristic multipliers may need network-specific tuning
4. **Integration points** - Exact code locations may need adjustment based on EPANET version
5. **Data structure modifications** - May impact memory usage and require optimization

## Success Metrics

- Achieve accuracy improvements similar to Table 1 results on sample network
- Maintain or improve computational performance
- Handle both pressure and velocity constraints correctly
- Demonstrate critical element identification matching expected behavior

This plan assumes iterative development with frequent validation checkpoints, as the missing implementation details will require experimental determination.
